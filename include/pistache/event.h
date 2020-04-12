/* event.h
 Copyright WING, 09.04.2020
 
 Make it easy to have bitwise operators for scoped or unscoped enumerations
 */

#pragma once

#include <iostream>

//// Abstract class for eventfd
//class Event {
//public:
//    static Event& dw
//
//    // Equivalent: eventfd_write()
//    virtual static void notify() = 0;
//
//    // Equivalent: eventfd_read()
//    virtual static void test() = 0;
//
//    // Equivalent: eventfd()
//    virtual static void init() = 0;
//
//private:
//    Event() {}
//    Event(const Event&);
//    Event& operator = (const Event&);
//}

#ifdef __MACH__
#include <sys/types.h>
#include <sys/event.h>
#include <sys/time.h>

#define EFD_NONBLOCK 0
#define EFD_CLOEXEC 1

// EventId = kq
typedef int EventId;
typedef uint64_t EventValue;

inline EventId event_init(unsigned int initval, int flags) {
    // One kqueue per event will be created. This is most likely very imperformant,
    // but we will stick to this design until we have worked something out.
    
    int kq = kqueue();
    assert(kq >= 0 && "Invalid kq return code");
    
    struct kevent event;
    EV_SET(&event, 1, EVFILT_USER, EV_ADD | EV_CLEAR, 0, 0, NULL);
    int ret = kevent(kq, &event, 1, NULL, 0, NULL);
    assert(ret == 0 && "Invalid kevent ret code");
    
    std::cout << "event_init kq=" << kq << std::endl;
    
    return kq;
}

inline int event_notify(EventId eid, EventValue value) {
    std::cout << "event_notify, kq=" << eid << ", value = " << value << std::endl;
    struct timespec timeout = { 0, 0 };  // wait-free
    struct kevent event;
    EV_SET(&event, 1, EVFILT_USER, 0, NOTE_TRIGGER, 0, reinterpret_cast<void*>(value));
    return kevent(eid, &event, 1, NULL, 0, &timeout);
}

inline int event_test(EventId eid, EventValue* value) {
    std::cout << "event_test, kq=" << eid << std::endl;
    struct timespec timeout = { 0, 0 }; // wait-free
    struct kevent event;
    int count = kevent(eid, NULL, 0, &event, 1, &timeout);
    if (count == 1) *value = reinterpret_cast<EventValue>(event.udata);
    std::cout << " count=" << count << ", value=" << (*value) << std::endl;
    return count;
}

//    inline int event_notify()
#else // __MACH__
#include <sys/eventfd.h>

// EventId = eventfd file descriptor
typedef int EventId;
typedef eventfd_t EventValue;

inline EventId event_init(unsigned int initval, int flags) {
    return eventfd(initval, flags);
}

inline int event_notify(EventId eid, EventValue value) {
    eventfd_write(eid, value);
}

inline int event_test(EventId eid, EventValue* value) {
    eventfd_read(eid, value);
}

//    inline int event_notify()
#endif // __MACH__
