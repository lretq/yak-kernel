#pragma once

#include <cstdint>
#include <yak/assert.h>
#include <yak/config.h>
#include <yak/panic.h>
#include <yak/thread.h>

namespace yak {

struct RunQueue {
  uint64_t ready_mask = 0;
  ThreadQueue queues[sched_prio::PRIO_COUNT];

  inline void verify() const {
#if CONFIG_DEBUG
    for (unsigned int i = 0; i < sched_prio::PRIO_COUNT; i++) {
      bool bit_set = (ready_mask & (1ULL << i)) != 0;
      bool queue_empty = queues[i].empty();
      assert(bit_set == !queue_empty &&
             "ready_mask bit inconsistent with queue state");
    }
    assert(sched_prio::PRIO_COUNT <= 64 &&
           "PRIO_COUNT exceeds ready_mask capacity");
#endif
  }

  inline void insert(Thread *thread) {
    auto prio = std::to_underlying(thread->priority_);
    assert(prio < sched_prio::PRIO_COUNT && "priority out of range");
    assert(prio < 64 && "priority exceeds ready_mask bit width");
    auto &queue = queues[prio];
    queue.push_back(thread);
    ready_mask |= (1ULL << prio);
    verify();
  }

  inline void remove(Thread *thread) {
    auto prio = std::to_underlying(thread->priority_);
    assert(prio < sched_prio::PRIO_COUNT && "priority out of range");
    assert(prio < 64 && "priority exceeds ready_mask bit width");
    assert(!queues[prio].empty() && "removing from empty priority queue");
#if CONFIG_DEBUG
    do {
      size_t found = 0;
      for (auto *t : queues[prio]) {
        if (t == thread)
          found++;
      }
      if (found)
        break;
      if (found == 0)
        panic("thread not found in queue");
      else if (found == 1)
        break;
      else
        panic("thread duplicated in queue");
    } while (true);
#endif
    auto &queue = queues[prio];
    queue.erase(thread);
    if (queue.empty()) {
      ready_mask &= ~(1ULL << prio);
    }
    verify();
  }

  inline Thread *pop(SchedPrio sprio) {
    auto prio = std::to_underlying(sprio);
    assert(prio < sched_prio::PRIO_COUNT && "priority out of range");
    assert(prio < 64 && "priority exceeds ready_mask bit width");
    assert(!queues[prio].empty() && "popping from empty priority queue");
    assert((ready_mask & (1ULL << prio)) &&
           "popping from priority not set in ready_mask");
    auto &queue = queues[prio];
    auto t = queue.pop_front();
    if (queue.empty()) {
      ready_mask &= ~(1ULL << prio);
    }
    verify();
    return t;
  }

  inline SchedPrio priority_ceil() const {
    assert(ready_mask != 0 && "priority_ceil called on empty RunQueue");
    // std::countl_zero counts leading zeros; subtracting from 63 gives
    // the index of the highest set bit, i.e. the highest-priority level.
    auto ceil = 63 - std::countl_zero(ready_mask);
    return SchedPrio(ceil);
  }

  inline bool empty() const {
    return ready_mask == 0;
  }

  inline bool has_thread(SchedPrio priority) const {
    auto under = std::to_underlying(priority);
    assert(under < sched_prio::PRIO_COUNT && "priority out of range");
    assert(under < 64 && "priority exceeds ready_mask bit width");
    return (ready_mask & (1ULL << under)) != 0;
  }
};

} // namespace yak
