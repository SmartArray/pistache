/* timer.h
 Copyright WING, 09.04.2020
 
 Make it easy to have bitwise operators for scoped or unscoped enumerations
 */

#pragma once

#include <iostream>

#ifdef __MACH__
#include <sys/types.h>
#include <sys/event.h>
#include <sys/time.h>

#define TFD_NONBLOCK 0
typedef int TimerStore; // kq
typedef size_t TimerId;

inline TimerStore timer_store() {
    TimerStore kq = kqueue();
    std::cout << "timer_store() kq=" << kq << std::endl; // YOSHI
    return kq;
}

inline TimerId timer_init(unsigned int initval, int flags, TimerId tid) {
    // Do nothing, except returning the timerid. Timer will be added with timer_set.
    // On Linux, a filedescriptor will be created using timerfd_create.
    std::cout << "timer_init() kq=" << tid << std::endl; // YOSHI
    (void)(initval);
    (void)(flags);
    return tid;
}

inline int timer_disarm(TimerStore store, TimerId tid) {
    struct kevent event;
    std::cout << "timer_init() kq=" << store << ", timer=" << tid << std::endl; // YOSHI
    EV_SET(&event, (uintptr_t) tid, EVFILT_TIMER, EV_DELETE, 0, 0, NULL);
    return kevent(store, &event, 1, NULL, 0, NULL);
}

inline int timer_set(TimerStore store, TimerId tid, std::chrono::milliseconds value) {
    struct kevent event;
    std::cout << "timer_init() kq=" << store << ", timer=" << tid << std::endl; // YOSHI
    EV_SET(&event, (uintptr_t) tid, EVFILT_TIMER, EV_ADD | EV_ENABLE, 0, value.count(), NULL);
    return kevent(store, &event, 1, NULL, 0, NULL);
}

#else // __MACH__

#include <sys/timerfd.h>
typedef void TimerStore;
typedef int TimerId;
typedef eventfd_t EventValue;

// Obsolete on linux
inline TimerStore timer_store() {
    return;
}

inline TimerId timer_init(unsigned int initval, int flags, TimerId tid) {
    return timerfd_create(initval, flags);
}

// On Linux, store will be ignored, tid is the file descriptor.
inline int timer_disarm(TimerStore store, TimerId tid) {
    itimerspec spec;
    spec.it_interval.tv_sec = 0;
    spec.it_interval.tv_nsec = 0;

    spec.it_value.tv_sec = 0;
    spec.it_value.tv_nsec = 0;
    
    return timerfd_settime(tid, 0, &spec, 0);
}

inline int timer_set(TimerStore store, TimerId tid, std::chrono::milliseconds value) {
    itimerspec spec;
    spec.it_interval.tv_sec = 0;
    spec.it_interval.tv_nsec = 0;

    if (value.count() < 1000) {
      spec.it_value.tv_sec = 0;
      spec.it_value.tv_nsec =
          std::chrono::duration_cast<std::chrono::nanoseconds>(value).count();
    } else {
      spec.it_value.tv_sec =
          std::chrono::duration_cast<std::chrono::seconds>(value).count();
      spec.it_value.tv_nsec = 0;
    }
    
    return timerfd_settime(tid, flags, new_value, old_value);
}

#endif // __MACH__
