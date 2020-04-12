/* timer_pool.cc
   Mathieu Stefani, 09 février 2016

   Implementation of the timer pool
*/

#include <pistache/os.h>
#include <pistache/timer_pool.h>
#include <pistache/timer.h>

#include <cassert>

#ifdef __MACH__
    #define DISARM(t) timer_disarm(kq, uid)
#else
    #define DISARM(t) timer_disarm(t)
#endif

#ifdef __MACH__
    #define ARM(t, d) timer_set(kq, t, d)
#else
    #define ARM(t, d) timer_set(t, d)
#endif

#ifdef __MACH__
    #define INIT(c, f) timer_init(c, f, uid)
#else
    #define INIT(c, f) timer_init(c, f)
#endif

namespace Pistache {

TimerPool::Entry::Entry() : fd_(-1), registered(false) {
  state.store(static_cast<uint32_t>(State::Idle));
}

TimerPool::Entry::~Entry() {
  if (fd_ != -1)
    close(fd_);
}

Fd TimerPool::Entry::fd() const {
  assert(fd_ != -1);

  return fd_;
}

void TimerPool::Entry::initialize() {
  if (fd_ == -1) {
    fd_ = TRY_RET(INIT(CLOCK_MONOTONIC, TFD_NONBLOCK));
  }
}

void TimerPool::Entry::disarm() {
  assert(fd_ != -1);
  TRY(DISARM(fd_));
}

void TimerPool::Entry::registerReactor(const Aio::Reactor::Key &key,
                                       Aio::Reactor *reactor) {
  if (!registered) {
    reactor->registerFd(key, fd_, Polling::NotifyOn::Read);
    registered = true;
  }
}

void TimerPool::Entry::armMs(std::chrono::milliseconds value) {
  TRY(ARM(fd_, value));
}

TimerPool::TimerPool(size_t initialSize) {
#ifdef __MACH__
  // Create a kqueue on BSD
  kq = timer_store();
#endif
    
  for (size_t i = 0; i < initialSize; ++i) {
    auto entry = std::make_shared<TimerPool::Entry>();
      
#ifdef __MACH__
      // Assign kq and uid to newly created entry.
      entry->uid = i;
      entry->kq = kq;
#endif
      
    timers.push_back(entry);
  }
}

std::shared_ptr<TimerPool::Entry> TimerPool::pickTimer() {
  for (auto &entry : timers) {
    auto curState = static_cast<uint32_t>(TimerPool::Entry::State::Idle);
    auto newState = static_cast<uint32_t>(TimerPool::Entry::State::Used);
    if (entry->state.compare_exchange_strong(curState, newState)) {
      entry->initialize();
      return entry;
    }
  }

  return nullptr;
}

void TimerPool::releaseTimer(const std::shared_ptr<Entry> &timer) {
  timer->state.store(static_cast<uint32_t>(TimerPool::Entry::State::Idle));
}

} // namespace Pistache
