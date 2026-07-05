#include <yak/arch-pcb.h>
#include <yak/arch.h>
#include <yak/panic.h>
#include <yak/sched.h>
#include <yak/thread.h>

namespace yak {

extern "C" {
void __x86_sched_switch(arch::ThreadPcb *current, arch::ThreadPcb *thread);
void __x86_sched_trampoline();
}

void Thread::init_context(void *kstack_top, ThreadEntryFn entry, void *ctx1,
                          void *ctx2) {
  kernel_stack_top = kstack_top;

  uint64_t *sp = reinterpret_cast<uint64_t *>(kstack_top);

  sp -= 2;
  *sp = (uint64_t) __x86_sched_trampoline;

  md.rsp = reinterpret_cast<uintptr_t>(sp);
  md.r12 = reinterpret_cast<uintptr_t>(entry);
  md.r13 = reinterpret_cast<uintptr_t>(ctx1);
  md.r14 = reinterpret_cast<uintptr_t>(ctx2);

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

  __x86_sched_switch(&current->md, &thread->md);
}
} // namespace arch
} // namespace yak
