#define pr_fmt(fmt) "vm: " fmt

#include <yak/arch-mm.h>
#include <yak/arch-stack.h>
#include <yak/log.h>
#include <yak/panic.h>
#include <yak/util.h>
#include <yak/vm/flags.h>
#include <yak/vm/pmm.h>
#include <yak/vm/stack.h>

namespace yak {

vaddr_t kstack_alloc() {
  const unsigned int kstack_order = pmm_size_to_order(arch::KSTACK_SIZE);
  auto pg = pmm_alloc(kstack_order, PageUse::Wired, ALLOC_ZERO);
  expect(pg, "sleepable kstack allocation failed!");
  auto bottom = pg.value()->to_pa() + arch::HHDM_BASE;
  pr_debug("alloc kstack with bottom at %lx\n", bottom);

  return bottom + arch::KSTACK_SIZE;
}

void kstack_free(vaddr_t stack_top) {
  panic("impl");
}

} // namespace yak
