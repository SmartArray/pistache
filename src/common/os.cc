/* os.cc
   Mathieu Stefani, 13 August 2015

*/

#include <pistache/common.h>
#include <pistache/config.h>
#include <pistache/os.h>

#ifdef __MACH__
#include <sys/types.h>
#include <sys/event.h>
#include <sys/time.h>
#else // __MACH__
#include <sys/epoll.h>
#include <sys/eventfd.h>
#endif // __MACH__

#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <thread>

namespace Pistache {

uint hardware_concurrency() { return std::thread::hardware_concurrency(); }

bool make_non_blocking(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags == -1)
    return false;

  flags |= O_NONBLOCK;
  int ret = fcntl(fd, F_SETFL, flags);
  if (ret == -1)
    return false;

  return true;
}

CpuSet::CpuSet() { bits.reset(); }

CpuSet::CpuSet(std::initializer_list<size_t> cpus) { set(cpus); }

void CpuSet::clear() { bits.reset(); }

CpuSet &CpuSet::set(size_t cpu) {
  if (cpu >= Size) {
    throw std::invalid_argument("Trying to set invalid cpu number");
  }

  bits.set(cpu);
  return *this;
}

CpuSet &CpuSet::unset(size_t cpu) {
  if (cpu >= Size) {
    throw std::invalid_argument("Trying to unset invalid cpu number");
  }

  bits.set(cpu, false);
  return *this;
}

CpuSet &CpuSet::set(std::initializer_list<size_t> cpus) {
  for (auto cpu : cpus)
    set(cpu);
  return *this;
}

CpuSet &CpuSet::unset(std::initializer_list<size_t> cpus) {
  for (auto cpu : cpus)
    unset(cpu);
  return *this;
}

CpuSet &CpuSet::setRange(size_t begin, size_t end) {
  if (begin > end) {
    throw std::range_error("Invalid range, begin > end");
  }

  for (size_t cpu = begin; cpu < end; ++cpu) {
    set(cpu);
  }

  return *this;
}

CpuSet &CpuSet::unsetRange(size_t begin, size_t end) {
  if (begin > end) {
    throw std::range_error("Invalid range, begin > end");
  }

  for (size_t cpu = begin; cpu < end; ++cpu) {
    unset(cpu);
  }

  return *this;
}

bool CpuSet::isSet(size_t cpu) const {
  if (cpu >= Size) {
    throw std::invalid_argument("Trying to test invalid cpu number");
  }

  return bits.test(cpu);
}

size_t CpuSet::count() const { return bits.count(); }

cpu_set_t CpuSet::toPosix() const {
  cpu_set_t cpu_set;
  CPU_ZERO(&cpu_set);

  for (size_t cpu = 0; cpu < Size; ++cpu) {
    if (bits.test(cpu))
      CPU_SET(cpu, &cpu_set);
  }

  return cpu_set;
}

#ifdef __MACH__
//#include <pistache/polling_compat.h>
    
namespace Polling {
    
    Event::Event(Tag _tag) : flags(), fd(-1), tag(_tag) {}
    
    Epoll::Epoll()
    : poll_id([&]() {
        return TRY_RET(kqueue());
    }()) {}
    
    Epoll::~Epoll() {
        if (poll_id >= 0) {
            close(poll_id);
        }
    }
    
    void Epoll::addFd(Fd fd, Flags<NotifyOn> interest, Tag tag, Mode mode) {
std::cout << "addFd fd=" << fd << " with value = " << tag.value_ << std::endl;  // YOSHI
        struct kevent ke;
        uint16_t flags = EV_ADD | EV_EOF | EV_ENABLE;
        uint16_t filter = static_cast<uint16_t>(toEpollEvents(interest));
        
        if (mode == Mode::Edge) flags |= EV_CLEAR;
        
        EV_SET(&ke, fd, filter, flags, 0, 0, (void*) tag.value_);
        TRY(kevent(poll_id, &ke, 1, NULL, 0, NULL));
    }
    
    void Epoll::addFdOneShot(Fd fd, Flags<NotifyOn> interest, Tag tag, Mode mode) {
//        struct epoll_event ev;
//        ev.events = toEpollEvents(interest);
//        ev.events |= EPOLLONESHOT;
//        if (mode == Mode::Edge)
//        ev.events |= EPOLLET;
//        ev.data.u64 = tag.value_;
//
//        TRY(epoll_ctl(poll_id, EPOLL_CTL_ADD, fd, &ev));
std::cout << "addFdOneShot with value = " << tag.value_ << std::endl;  // YOSHI
        
        struct kevent ke;
        uint16_t flags = EV_ADD | EV_EOF | EV_ENABLE;
        uint16_t filter = static_cast<uint16_t>(toEpollEvents(interest));
        
        flags |= EV_ONESHOT;
        if (mode == Mode::Edge) flags |= EV_CLEAR;
        
        EV_SET(&ke, fd, filter, flags, 0, 0, (void*) tag.value_);
        TRY(kevent(poll_id, &ke, 1, NULL, 0, NULL));
    }
    
    // Additional param: interest, since kqueue filters are unique tuples (ident|filter|udata)
    void Epoll::removeFd(Fd fd, Flags<NotifyOn> interest) {
        //        struct epoll_event ev;
        //        TRY(epoll_ctl(poll_id, EPOLL_CTL_DEL, fd, &ev));
        struct kevent ke;
        ke.ident = static_cast<uintptr_t>(fd);
        ke.filter = static_cast<int16_t>(toEpollEvents(interest));
        ke.flags = EV_DELETE;
        TRY(kevent(poll_id, &ke, 1, NULL, 0, NULL));
        
        std::cout << "Removed fd = " << fd << std::endl;
    }
    
    void Epoll::rearmFd(Fd fd, Flags<NotifyOn> interest, Tag tag, Mode mode) {
//        struct epoll_event ev;
//        ev.events = toEpollEvents(interest);
//        if (mode == Mode::Edge)
//        ev.events |= EPOLLET;
//        ev.data.u64 = tag.value_;
//
//        TRY(epoll_ctl(poll_id, EPOLL_CTL_MOD, fd, &ev));
        
        // Readding interest will modify the filedescriptor
std::cout << "Rearm fd = " << fd << ", tag=" << tag.value_ << std::endl;
        return addFd(fd, interest, tag, mode);
    }
    
    int Epoll::poll(std::vector<Event> &events,
                    const std::chrono::milliseconds timeout) const {
//        struct epoll_event evs[Const::MaxEvents];
//
//        int ready_fds = -1;
//        do {
//            ready_fds = ::epoll_wait(poll_id, evs, Const::MaxEvents, static_cast<int>(timeout.count()));
//        } while (ready_fds < 0 && errno == EINTR);
//
//        for (int i = 0; i < ready_fds; ++i) {
//            const struct epoll_event *ev = evs + i;
//
//            const Tag tag(ev->data.u64);
//
//            Event event(tag);
//            event.flags = toNotifyOn(ev->events);
//            events.push_back(event);
//        }
//
//        return ready_fds;
        struct kevent triggered[Const::MaxEvents]; // Big stack overhead, should output this to calling instance.
        struct timespec to = { 0, 0 };
        int n_triggered_events = -1;
        
        if (timeout.count() > 0) {
            to.tv_sec = std::chrono::duration_cast<std::chrono::seconds>(timeout).count();
            to.tv_nsec = std::chrono::duration_cast<std::chrono::microseconds>(timeout).count();
        }
        
        do {
            n_triggered_events = kevent(poll_id, NULL, 0, &triggered[0], Const::MaxEvents, &to);
        } while (n_triggered_events < 0 && errno == EINTR);
        
        for (int i = 0; i < n_triggered_events; ++i) {
            const struct kevent* ev = triggered + i;
            const Tag tag(reinterpret_cast<uint64_t>(ev->udata));
            
//            std::cerr << "triggered_event with value = " << reinterpret_cast<uint64_t>(ev->udata) << std::endl; // YOSHI
            
            // Create Event object and push it to result list.
            Event event(tag);
            event.flags = toNotifyOn(ev->filter);
            if (ev->flags & EV_EOF) { event.flags.setFlag(NotifyOn::Shutdown);
                
                std::cerr << "Shutdown " << std::endl; // Yoshi
            }
            events.push_back(event);
        }
        
        
//        std::cerr << "n_triggered_events=" << n_triggered_events << std::endl;
        
        return n_triggered_events;
    }
    
    int Epoll::toEpollEvents(const Flags<NotifyOn> &interest) {
        int events = 0;
        
        if (interest.hasFlag(NotifyOn::Read))
            events |= EVFILT_READ;
        if (interest.hasFlag(NotifyOn::Write))
            events |= EVFILT_WRITE;
        if (interest.hasFlag(NotifyOn::Hangup))
            events |= EVFILT_EXCEPT;
        if (interest.hasFlag(NotifyOn::Shutdown))
            events |= EVFILT_EXCEPT;
        
        if (false) {
        if (interest.hasFlag(NotifyOn::Read))
        std::cerr << "toEpollEvents => read" << std::endl;  // YOSHI
        if (interest.hasFlag(NotifyOn::Write))
        std::cerr << "toEpollEvents => write" << std::endl;
        if (interest.hasFlag(NotifyOn::Hangup))
        std::cerr << "toEpollEvents => hangup" << std::endl;
        if (interest.hasFlag(NotifyOn::Shutdown))
        std::cerr << "toEpollEvents => shutdown" << std::endl;
        }
        
        return events;
    }
    
    Flags<NotifyOn> Epoll::toNotifyOn(int events) {
        Flags<NotifyOn> flags;
        
        if (events & EVFILT_READ)
        flags.setFlag(NotifyOn::Read);
        if (events & EVFILT_WRITE)
        flags.setFlag(NotifyOn::Write);
        if (events & EVFILT_EXCEPT) {
            flags.setFlag(NotifyOn::Hangup);
        }
        
        return flags;
    }
    
} // namespace Polling
    
#else

namespace Polling {
    
Event::Event(Tag _tag) : flags(), fd(-1), tag(_tag) {}

Epoll::Epoll()
    : poll_id([&]() { return TRY_RET(epoll_create(Const::MaxEvents)); }()) {}

Epoll::~Epoll() {
  if (poll_id >= 0) {
    close(poll_id);
  }
}

void Epoll::addFd(Fd fd, Flags<NotifyOn> interest, Tag tag, Mode mode) {
  struct epoll_event ev;
  ev.events = toEpollEvents(interest);
  if (mode == Mode::Edge)
    ev.events |= EPOLLET;
  ev.data.u64 = tag.value_;

  TRY(epoll_ctl(poll_id, EPOLL_CTL_ADD, fd, &ev));
}

void Epoll::addFdOneShot(Fd fd, Flags<NotifyOn> interest, Tag tag, Mode mode) {
  struct epoll_event ev;
  ev.events = toEpollEvents(interest);
  ev.events |= EPOLLONESHOT;
  if (mode == Mode::Edge)
    ev.events |= EPOLLET;
  ev.data.u64 = tag.value_;

  TRY(epoll_ctl(poll_id, EPOLL_CTL_ADD, fd, &ev));
}

void Epoll::removeFd(Fd fd) {
  struct epoll_event ev;
  TRY(epoll_ctl(poll_id, EPOLL_CTL_DEL, fd, &ev));
}

void Epoll::rearmFd(Fd fd, Flags<NotifyOn> interest, Tag tag, Mode mode) {
  struct epoll_event ev;
  ev.events = toEpollEvents(interest);
  if (mode == Mode::Edge)
    ev.events |= EPOLLET;
  ev.data.u64 = tag.value_;

  TRY(epoll_ctl(poll_id, EPOLL_CTL_MOD, fd, &ev));
}

int Epoll::poll(std::vector<Event> &events,
                const std::chrono::milliseconds timeout) const {
  struct epoll_event evs[Const::MaxEvents];

  int ready_fds = -1;
  do {
    ready_fds = ::epoll_wait(poll_id, evs, Const::MaxEvents, static_cast<int>(timeout.count()));
  } while (ready_fds < 0 && errno == EINTR);

  for (int i = 0; i < ready_fds; ++i) {
    const struct epoll_event *ev = evs + i;

    const Tag tag(ev->data.u64);

    Event event(tag);
    event.flags = toNotifyOn(ev->events);
    events.push_back(event);
  }

  return ready_fds;
}

int Epoll::toEpollEvents(const Flags<NotifyOn> &interest) {
  int events = 0;

  if (interest.hasFlag(NotifyOn::Read))
    events |= EPOLLIN;
  if (interest.hasFlag(NotifyOn::Write))
    events |= EPOLLOUT;
  if (interest.hasFlag(NotifyOn::Hangup))
    events |= EPOLLHUP;
  if (interest.hasFlag(NotifyOn::Shutdown))
    events |= EPOLLRDHUP;

  return events;
}

Flags<NotifyOn> Epoll::toNotifyOn(int events) {
  Flags<NotifyOn> flags;

  if (events & EPOLLIN)
    flags.setFlag(NotifyOn::Read);
  if (events & EPOLLOUT)
    flags.setFlag(NotifyOn::Write);
  if (events & EPOLLHUP)
    flags.setFlag(NotifyOn::Hangup);
  if (events & EPOLLRDHUP) {
    flags.setFlag(NotifyOn::Shutdown);
  }

  return flags;
}
    
} // namespace Polling
#endif // __MACH__
    
NotifyFd::NotifyFd() : event_id(-1) {}

Polling::Tag NotifyFd::bind(Polling::Epoll &poller) {
  event_id = TRY_RET(event_init(0, EFD_NONBLOCK | EFD_CLOEXEC));
  Polling::Tag tag(event_id);

  poller.addFd(event_id, Flags<Polling::NotifyOn>(Polling::NotifyOn::Read), tag,
               Polling::Mode::Edge);
  return tag;
}

bool NotifyFd::isBound() const { return event_id != -1; }

Polling::Tag NotifyFd::tag() const { return Polling::Tag(event_id); }

void NotifyFd::notify() const {
  if (!isBound())
    throw std::runtime_error("Can not notify an unbound fd");
  EventValue val = 1;
    
    std::cout << "notify() from os.cc" << std::endl; // YOSHI
  TRY(event_notify(event_id, val));
}

void NotifyFd::read() const {
    std::cout << "read() from os.cc" << std::endl; // YOSHI
  if (!isBound())
    throw std::runtime_error("Can not read an unbound fd");
  EventValue val;
  TRY(event_test(event_id, &val));
}

bool NotifyFd::tryRead() const {
  EventValue val;
  int res = event_test(event_id, &val);
  if (res == -1) {
    if (errno == EAGAIN || errno == EWOULDBLOCK)
      return false;
    throw std::runtime_error("Failed to read eventfd");
  }

  return true;
}

} // namespace Pistache
