#include <yak/assert.h>
#include <yak/cpudata.h>
#include <yak/ipl.h>
#include <yak/log.h>

namespace yak {

#if !YAK_HOSTED_MODE
[[noreturn]]
void idle_loop() {
  iplx(Ipl::passive);

  while (true) {
    assert(iplget() == Ipl::passive);

    pr_debug("core %zu is idle!\n", CPUDATA_LOAD(id));

    arch::enable_interrupts();

#if defined x86_64
    // TODO: implement monitor/mwait, proper idle driver?
    arch::interrupt_wait();
#elif defined riscv64
    arch::interrupt_wait();
#else
#error "Port idle_loop to this architecture!"
#endif
  }
}
#endif

} // namespace yak
