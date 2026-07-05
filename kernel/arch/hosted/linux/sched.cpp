#include "yak/log.h"
#include "yak/percpu.h"
#include <yak/arch-intr.h>
#include <yak/cpudata.h>
#include <yak/sched.h>
#include <yak/thread.h>

namespace yak {

void Thread::init_context(void *kstack_top, ThreadEntryFn entry, void *ctx1,
                          void *ctx2) {}

[[noreturn]]
void idle_loop() {
  while (true) {
    pr_debug("linux vcpu %zu is idle!\n", CPUDATA_LOAD(id));
    arch::interrupt_wait();
    // XXX: idea for idle/interrupt wait:
    // eventfd that are sent by other vCPUs or e.g. timers
    sleep(9999);
  }
}

namespace arch {

void sched_switch(Thread *current, Thread *thread) {}
} // namespace arch
} // namespace yak
