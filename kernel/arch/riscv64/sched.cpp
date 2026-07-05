#include <yak/arch-pcb.h>
#include <yak/arch.h>
#include <yak/panic.h>
#include <yak/sched.h>
#include <yak/thread.h>

namespace yak {

extern "C" {
void __riscv64_sched_switch(arch::ThreadPcb *current, arch::ThreadPcb *thread);
void __riscv64_sched_trampoline();
}

void Thread::init_context(void *kstack_top, ThreadEntryFn entry, void *ctx1,
                          void *ctx2) {
  kernel_stack_top_ = kstack_top;

  uint64_t *sp = reinterpret_cast<uint64_t *>(kstack_top);

  md.ra = (uint64_t) __riscv64_sched_trampoline;
  md.sp = (uint64_t) sp;
  md.s2 = (uint64_t) entry;
  md.s3 = (uint64_t) ctx1;
  md.s4 = (uint64_t) ctx2;

  if (is_user()) {
    panic("FPU alloc");
  }
}

namespace arch {
void sched_switch(Thread *current, Thread *thread) {
  if (current->is_user()) {
    panic("FPU save");
  }

  if (thread->is_user()) {
    panic("tss, fpu -> user setup");
  }

  __riscv64_sched_switch(&current->md, &thread->md);
}
} // namespace arch
} // namespace yak
