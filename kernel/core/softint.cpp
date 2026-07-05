#include <array>
#include <yak/assert.h>
#include <yak/arch-intr.h>
#include <yak/interrupt-guard.h>
#include <yak/panic.h>
#include <yak/softint.h>

namespace yak {

static inline uint32_t pending_for(Ipl ipl) {
  return 1 << (std::to_underlying(ipl) - 1);
}

// Mask of all softint bits > ipl
static inline uint32_t pending_above(Ipl ipl) {
  const uint32_t idx = std::to_underlying(ipl) - 1;
  return ~0u << (idx + 1);
}

using SoftintHandler = void (*)(CpuData *);
constinit std::array<SoftintHandler, std::to_underlying(Ipl::high) - 1>
    handlers = {
        nullptr, /* TODO: apc handler */
        [](CpuData *cpu) {
          cpu->softints_pending.fetch_and(~pending_for(Ipl::dispatch));

          iplr(Ipl::dispatch);

          // softint_dispatch disabled interrupts
          arch::enable_interrupts();

          cpu->dpc_queue->drain();

          cpu->sched->commit_reschedule();

          // restore previous state
          arch::disable_interrupts();
        },
};

void softint_dispatch(Ipl ipl) {
  InterruptGuard ints{};

  auto cpu = CpuData::current();
  uint32_t pending;

  while ((pending = cpu->softints_pending.load(std::memory_order_acquire)) &
         pending_above(ipl)) {
    uint32_t masked = pending & pending_above(ipl);
    int idx = 31 - __builtin_clz(masked);
    assert(handlers[idx] != nullptr);
    handlers[idx](cpu);
    cpu = CpuData::current();
  }

  iplx(ipl);
}

void softint_issue(Ipl ipl) {
  CpuData::current()->softints_pending.fetch_or(pending_for(ipl),
                                                std::memory_order_release);
}

void softint_issue_other(CpuData *cpu, Ipl ipl) {
  cpu->softints_pending.fetch_or(pending_for(ipl), std::memory_order_release);
  panic("send ipi");
}
} // namespace yak
