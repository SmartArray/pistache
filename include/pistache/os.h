/* os.h
   Mathieu Stefani, 13 August 2015

   Operating system specific functions
*/

#pragma once

#include <pistache/common.h>
#include <pistache/config.h>
#include <pistache/flags.h>
#include <pistache/event.h>
#include <pistache/timer.h>

#include <bitset>
#include <chrono>
#include <memory>
#include <vector>

#include <sched.h>

#ifdef __MACH__
typedef struct {
    uint64_t count;
} cpu_set_t;

static inline void CPU_ZERO(cpu_set_t* s) { s->count = 0; }
static inline void CPU_SET(uint64_t num, cpu_set_t* s) { s->count |= (1 << num); }
static inline int CPU_ISSET(uint64_t num, cpu_set_t* s) { return static_cast<int>(s->count & (1 << num)); }
#endif

namespace Pistache {

using Fd = int;

uint hardware_concurrency();
bool make_non_blocking(int fd);

class CpuSet {
public:
  static constexpr size_t Size = 1024;

  CpuSet();
  explicit CpuSet(std::initializer_list<size_t> cpus);

  void clear();
  CpuSet &set(size_t cpu);
  CpuSet &unset(size_t cpu);

  CpuSet &set(std::initializer_list<size_t> cpus);
  CpuSet &unset(std::initializer_list<size_t> cpus);

  CpuSet &setRange(size_t begin, size_t end);
  CpuSet &unsetRange(size_t begin, size_t end);

  bool isSet(size_t cpu) const;
  size_t count() const;

  cpu_set_t toPosix() const;

private:
  std::bitset<Size> bits;
};

namespace Polling {

enum class Mode { Level, Edge };

enum class NotifyOn {
  None = 0,

  Read = 1,
  Write = Read << 1,
  Hangup = Read << 2,
  Shutdown = Read << 3
};

DECLARE_FLAGS_OPERATORS(NotifyOn)

struct Tag {
  friend class Epoll;

  explicit constexpr Tag(uint64_t value) : value_(value) {}

  constexpr uint64_t value() const { return value_; }

  friend constexpr bool operator==(Tag lhs, Tag rhs);
  friend constexpr bool operator<(Tag lhs, Tag rhs) { return lhs.value_ < rhs.value_; }

private:
  uint64_t value_;
};

inline constexpr bool operator==(Tag lhs, Tag rhs) {
  return lhs.value_ == rhs.value_;
}

struct Event {
  explicit Event(Tag _tag);

  Flags<NotifyOn> flags;
  Fd fd;
  Tag tag;
};

class Epoll {
public:
  Epoll();
  ~Epoll();

  void addFd(Fd fd, Flags<NotifyOn> interest, Tag tag, Mode mode = Mode::Level, uint16_t initialFlags = 0);
  void addFdOneShot(Fd fd, Flags<NotifyOn> interest, Tag tag,
                    Mode mode = Mode::Level);

  void removeFd(Fd fd);
  void rearmFd(Fd fd, Flags<NotifyOn> interest, Tag tag,
               Mode mode = Mode::Level);

  int poll(std::vector<Event> &events, const std::chrono::milliseconds timeout =
                                           std::chrono::milliseconds(-1)) const;

private:
  static int toEpollEvents(const Flags<NotifyOn> &interest);
  static Flags<NotifyOn> toNotifyOn(int events);
  Fd poll_id;
};

} // namespace Polling

class NotifyFd {
public:
  NotifyFd();

  Polling::Tag bind(Polling::Epoll &poller);

  bool isBound() const;

  Polling::Tag tag() const;

  void notify() const;

  void read() const;
  bool tryRead() const;

private:
  EventId event_id;
};

} // namespace Pistache
