/* traqnsport.cc
   Mathieu Stefani, 02 July 2017

   TCP transport handling

*/

#ifdef __MACH__
    // sendfile
    #include <sys/types.h>
    #include <sys/socket.h>
    #include <sys/uio.h>
    // getrusage
    #include <sys/resource.h>

    #define TIMER_SET(fd, event) timer_set(kq, fd, event)
#else
    #include <sys/sendfile.h>
    #include <sys/time.h>

    #define TIMER_SET(fd, event) timer_set(fd, event)
#endif

#include <sys/resource.h>

#include <pistache/os.h>
#include <pistache/peer.h>
#include <pistache/tcp.h>
#include <pistache/transport.h>
#include <pistache/utils.h>
#include <pistache/timer.h>

namespace Pistache {

using namespace Polling;

namespace Tcp {

Transport::Transport(const std::shared_ptr<Tcp::Handler> &handler) {
  init(handler);
}

void Transport::init(const std::shared_ptr<Tcp::Handler> &handler) {
  handler_ = handler;
  handler_->associateTransport(this);
    
#ifdef __MACH__
  kq = timer_store();
#endif
}

std::shared_ptr<Aio::Handler> Transport::clone() const {
  return std::make_shared<Transport>(handler_->clone());
}

void Transport::registerPoller(Polling::Epoll &poller) {
  writesQueue.bind(poller);
  timersQueue.bind(poller);
  peersQueue.bind(poller);
  notifier.bind(poller);
}

void Transport::handleNewPeer(const std::shared_ptr<Tcp::Peer> &peer) {
  auto ctx = context();
  const bool isInRightThread = std::this_thread::get_id() == ctx.thread();
    
  if (!isInRightThread) {
    PeerEntry entry(peer);
    peersQueue.push(std::move(entry));
  } else {
    handlePeer(peer);
  }
    
  int fd = peer->fd();
  {
    Guard guard(toWriteLock);
    toWrite.emplace(fd, std::deque<WriteEntry>{});
  }
}

void Transport::onReady(const Aio::FdSet &fds) {
  for (const auto &entry : fds) {
    std::cout << "onReady()" << std::endl; // YOSHI
//    printStacktrace();
    
    if (entry.getTag() == writesQueue.tag()) {
        std::cout << "  writeQueue" << std::endl;
      handleWriteQueue();
    } else if (entry.getTag() == timersQueue.tag()) {
      handleTimerQueue();
        std::cout << "  timersQueue" << std::endl;
    } else if (entry.getTag() == peersQueue.tag()) {
      handlePeerQueue();
        std::cout << "  peersQueue" << std::endl;
    } else if (entry.getTag() == notifier.tag()) {
      handleNotify();
        std::cout << "  notify" << std::endl;
    }

    else if (entry.isReadable()) {
        std::cout << "  is readable" << std::endl;
      auto tag = entry.getTag();
      if (isPeerFd(tag)) {
        auto &peer = getPeer(tag);
        handleIncoming(peer);
      } else if (isTimerFd(tag)) {
        auto it = timers.find(static_cast<decltype(timers)::key_type>(tag.value()));
        auto &entry_ = it->second;
        handleTimer(std::move(entry_));
        timers.erase(it->first);
      } else {
        throw std::runtime_error("Unknown fd");
      }

    } else if (entry.isWritable()) {
        std::cout << "  is writable" << std::endl;
      auto tag = entry.getTag();
      auto fd = static_cast<Fd>(tag.value());

      {
        Guard guard(toWriteLock);
        std::cout << "Lookup Writedata " << fd << std::endl;
        auto it = toWrite.find(fd);
        if (it == std::end(toWrite)) {
          throw std::runtime_error(
              "Assertion Error: could not find write data");
        }
      }

      reactor()->modifyFd(key(), fd, NotifyOn::Read, Polling::Mode::Edge);

      // Try to drain the queue
        std::cout << "  call asyncWrite" << std::endl;
      asyncWriteImpl(fd);
    }
  }
}

void Transport::disarmTimer(Fd fd) {
  auto it = timers.find(fd);
  if (it == std::end(timers))
    throw std::runtime_error("Timer has not been armed");

  auto &entry = it->second;
  entry.disable();
}

void Transport::handleIncoming(const std::shared_ptr<Peer> &peer) {
  char buffer[Const::MaxBuffer] = {0};

  ssize_t totalBytes = 0;
  int fd = peer->fd();

  for (;;) {

    ssize_t bytes;

#ifdef PISTACHE_USE_SSL
    if (peer->ssl() != NULL) {
      bytes = SSL_read((SSL *)peer->ssl(), buffer + totalBytes,
                       static_cast<int>(Const::MaxBuffer - totalBytes));
    } else {
#endif /* PISTACHE_USE_SSL */
      bytes = recv(fd, buffer + totalBytes, Const::MaxBuffer - totalBytes, 0);
#ifdef PISTACHE_USE_SSL
    }
#endif /* PISTACHE_USE_SSL */

    if (bytes == -1) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        if (totalBytes > 0) {
          handler_->onInput(buffer, totalBytes, peer);
        }
      } else {
        if (errno == ECONNRESET) {
          handlePeerDisconnection(peer);
        } else {
          throw std::runtime_error(strerror(errno));
        }
      }
      break;
    } else if (bytes == 0) {
      handlePeerDisconnection(peer);
      break;
    }

    else {
      handler_->onInput(buffer, bytes, peer);
    }
  }
}

void Transport::handlePeerDisconnection(const std::shared_ptr<Peer> &peer) {
  handler_->onDisconnection(peer);

  int fd = peer->fd();
  auto it = peers.find(fd);
  if (it == std::end(peers))
    throw std::runtime_error("Could not find peer to erase");

  peers.erase(it->first);

  {
    // Clean up buffers
    Guard guard(toWriteLock);
    auto &wq = toWrite[fd];
    while (wq.size() > 0) {
      wq.pop_front();
    }
    toWrite.erase(fd);
  }

  close(fd);
}

void Transport::asyncWriteImpl(Fd fd) {
  bool stop = false;
  while (!stop) {
    Guard guard(toWriteLock);

    auto it = toWrite.find(fd);

    // cleanup will have been handled by handlePeerDisconnection
    if (it == std::end(toWrite)) {
      return;
    }
      
    auto &wq = it->second;
    if (wq.size() == 0) {
      break;
    }

    auto &entry = wq.front();
    int flags = entry.flags;
    BufferHolder &buffer = entry.buffer;
    Async::Deferred<ssize_t> deferred = std::move(entry.deferred);

    auto cleanUp = [&]() {
      wq.pop_front();
      if (wq.size() == 0) {
        toWrite.erase(fd);
        reactor()->modifyFd(key(), fd, NotifyOn::Read, Polling::Mode::Edge);
        stop = true;
      }
    };

    size_t totalWritten = buffer.offset();
    for (;;) {
      ssize_t bytesWritten = 0;
      auto len = buffer.size() - totalWritten;

      if (buffer.isRaw()) {
        auto raw = buffer.raw();
        auto ptr = raw.data().c_str() + totalWritten;

#ifdef PISTACHE_USE_SSL
        auto it_ = peers.find(fd);

        if (it_ == std::end(peers))
          throw std::runtime_error("No peer found for fd: " +
                                   std::to_string(fd));

        if (it_->second->ssl() != NULL) {
          auto ssl_ = static_cast<SSL *>(it_->second->ssl());
          bytesWritten = SSL_write(ssl_, ptr, static_cast<int>(len));
        } else {
#endif /* PISTACHE_USE_SSL */
          bytesWritten = ::send(fd, ptr, len, flags);
#ifdef PISTACHE_USE_SSL
        }
#endif /* PISTACHE_USE_SSL */
      } else {
        auto file = buffer.fd();
        off_t offset = totalWritten;

#ifdef PISTACHE_USE_SSL
        auto it_ = peers.find(fd);

        if (it_ == std::end(peers))
          throw std::runtime_error("No peer found for fd: " +
                                   std::to_string(fd));

        if (it_->second->ssl() != NULL) {
          auto ssl_ = static_cast<SSL *>(it_->second->ssl());
          bytesWritten = SSL_sendfile(ssl_, file, &offset, len);
        } else {
#endif /* PISTACHE_USE_SSL */
#if __MACH__
          bytesWritten = ::sendfile(fd, file, offset, &offset, NULL, 0);
#else
          bytesWritten = ::sendfile(fd, file, &offset, len);
#endif // __MACH__
#ifdef PISTACHE_USE_SSL
        }
#endif /* PISTACHE_USE_SSL */
      }
      if (bytesWritten < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {

          auto bufferHolder = buffer.detach(totalWritten);

          // pop_front kills buffer - so we cannot continue loop or use buffer
          // after this point
          wq.pop_front();
          wq.push_front(WriteEntry(std::move(deferred), bufferHolder, flags));
          reactor()->modifyFd(key(), fd, NotifyOn::Read | NotifyOn::Write,
                              Polling::Mode::Edge);
        }
        // EBADF can happen when the HTTP parser, in the case of
        // an error, closes fd before the entire request is processed.
        // https://github.com/oktal/pistache/issues/501
        else if (errno == EBADF || errno == EPIPE || errno == ECONNRESET) {
          wq.pop_front();
          toWrite.erase(fd);
          stop = true;
        } else {
          cleanUp();
          deferred.reject(Pistache::Error::system("Could not write data"));
        }
        break;
      } else {
        totalWritten += bytesWritten;
        if (totalWritten >= buffer.size()) {
          if (buffer.isFile()) {
            // done with the file buffer, nothing else knows whether to
            // close it with the way the code is written.
            ::close(buffer.fd());
          }

          cleanUp();

          // Cast to match the type of defered template
          // to avoid a BadType exception
          deferred.resolve(static_cast<ssize_t>(totalWritten));
          break;
        }
      }
    }
  }
}

void Transport::armTimerMs(Fd fd, std::chrono::milliseconds value,
                           Async::Deferred<uint64_t> deferred) {

  auto ctx = context();
  const bool isInRightThread = std::this_thread::get_id() == ctx.thread();
  TimerEntry entry(fd, value, std::move(deferred));

  if (!isInRightThread) {
    timersQueue.push(std::move(entry));
  } else {
    armTimerMsImpl(std::move(entry));
  }
}

void Transport::armTimerMsImpl(TimerEntry entry) {

  auto it = timers.find(entry.fd);
  if (it != std::end(timers)) {
    entry.deferred.reject(std::runtime_error("Timer is already armed"));
    return;
  }

  int res = TIMER_SET(entry.fd, entry.value);
  if (res == -1) {
    entry.deferred.reject(Pistache::Error::system("Could not set timer time"));
    return;
  }

  reactor()->registerFdOneShot(key(), entry.fd, NotifyOn::Read,
                               Polling::Mode::Edge);
  timers.insert(std::make_pair(entry.fd, std::move(entry)));
}

void Transport::handleWriteQueue() {
  // Let's drain the queue
  for (;;) {
      std::cout << "writesQueue.popSafe()" << std::endl; // YOSHI
    auto write = writesQueue.popSafe();
    if (!write) {
      std::cout << "BREAK" << std::endl; // YOSHI
      break;
    }

    auto fd = write->peerFd;
    if (!isPeerFd(fd))
      continue;

    {
      Guard guard(toWriteLock);
      std::cout << "Adding write data for fd = " << fd << std::endl;
      toWrite[fd].push_back(std::move(*write));
    }

    reactor()->modifyFd(key(), fd, NotifyOn::Read | NotifyOn::Write,
                        Polling::Mode::Edge);
  }
}

void Transport::handleTimerQueue() {
  for (;;) {
      std::cout << "timersQueue.popSafe()" << std::endl; // YOSHI
    auto timer = timersQueue.popSafe();
    if (!timer)
      break;

    armTimerMsImpl(std::move(*timer));
  }
}

void Transport::handlePeerQueue() {
  for (;;) {
      std::cout << "handlePeerQueue" << std::endl; // YOSHI
    auto data = peersQueue.popSafe();
    if (!data)
      break;
    handlePeer(data->peer);
  }
}

void Transport::handlePeer(const std::shared_ptr<Peer> &peer) {
  int fd = peer->fd();
  peers.insert(std::make_pair(fd, peer));

  peer->associateTransport(this);

  handler_->onConnection(peer);
  reactor()->registerFd(key(), fd, NotifyOn::Read | NotifyOn::Shutdown,
                        Polling::Mode::Edge);
}

void Transport::handleNotify() {
  while (this->notifier.tryRead())
    ;

  rusage now;
    
#ifdef __MACH__
  auto res = getrusage(RUSAGE_SELF, &now);
#else
  auto res = getrusage(RUSAGE_THREAD, &now);
#endif // __MACH__
    
  if (res == -1)
    loadRequest_.reject(std::runtime_error("Could not compute usage"));

  loadRequest_.resolve(now);
  loadRequest_.clear();
}

void Transport::handleTimer(TimerEntry entry) {
  if (entry.isActive()) {
    uint64_t numWakeups;
    auto res = ::read(entry.fd, &numWakeups, sizeof numWakeups);
    if (res == -1) {
      if (errno == EAGAIN || errno == EWOULDBLOCK)
        return;
      else
        entry.deferred.reject(
            Pistache::Error::system("Could not read timerfd"));
    } else {
      if (res != sizeof(numWakeups)) {
        entry.deferred.reject(
            Pistache::Error("Read invalid number of bytes for timer fd: " +
                            std::to_string(entry.fd)));
      } else {
        entry.deferred.resolve(numWakeups);
      }
    }
  }
}

bool Transport::isPeerFd(Fd fd) const {
  return peers.find(fd) != std::end(peers);
}

bool Transport::isTimerFd(Fd fd) const {
  return timers.find(fd) != std::end(timers);
}

bool Transport::isPeerFd(Polling::Tag tag) const {
  return isPeerFd(static_cast<Fd>(tag.value()));
}
bool Transport::isTimerFd(Polling::Tag tag) const {
  return isTimerFd(static_cast<Fd>(tag.value()));
}

std::shared_ptr<Peer> &Transport::getPeer(Fd fd) {
  auto it = peers.find(fd);
  if (it == std::end(peers)) {
    throw std::runtime_error("No peer found for fd: " + std::to_string(fd));
  }
  return it->second;
}

std::shared_ptr<Peer> &Transport::getPeer(Polling::Tag tag) {
  return getPeer(static_cast<Fd>(tag.value()));
}

} // namespace Tcp
} // namespace Pistache
