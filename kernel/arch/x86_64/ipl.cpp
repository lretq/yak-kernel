#include <x86_64/asm.h>
#include <yak/config.h>
#include <yak/cpudata.h>
#include <yak/ipl.h>
#include <yak/panic.h>
#include <yak/percpu.h>
#include <yak/softint.h>

namespace yak {

Ipl iplget() {
#if CONFIG_LAZY_IPL
  return CPUDATA_LOAD(md.soft_ipl);
#else
  return static_cast<Ipl>(asm_rdcr8());
#endif
}

Ipl iplr(Ipl ipl) {
  if (ipl < iplget()) [[unlikely]] {
    panic("IPL: raise ipl < current\n");
  }
#if CONFIG_LAZY_IPL
  return CPUDATA_XCHG(md.soft_ipl, ipl);
#else
  auto old = iplget();
  asm_wrcr8(static_cast<uint64_t>(ipl));
  return old;
#endif
}

void iplx(Ipl ipl) {
#if CONFIG_LAZY_IPL
  CPUDATA_STORE(md.soft_ipl, ipl);
  if (CPUDATA_LOAD(md.hard_ipl) > ipl) {
    CPUDATA_STORE(md.hard_ipl, ipl);
    asm_wrcr8(static_cast<uint64_t>(ipl));
  }
#else
  asm_wrcr8(static_cast<uint64_t>(ipl));
#endif

  if (CpuData::current()->softints_pending.load(std::memory_order_relaxed) >>
      std::to_underlying(ipl)) {
    softint_dispatch(ipl);
  }
}

} // namespace yak
