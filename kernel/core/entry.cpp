#include "yak/ipl-guard.h"
#include "yak/sched_prio.h"
#include <cstddef>
#include <span>
#include <yak/arch.h>
#include <yak/config.h>
#include <yak/cpudata.h>
#include <yak/event.h>
#include <yak/idle.h>
#include <yak/init.h>
#include <yak/kobject.h>
#include <yak/log.h>
#include <yak/math.h>
#include <yak/mutex.h>
#include <yak/percpu.h>
#include <yak/ps.h>
#include <yak/sched.h>
#include <yak/thread.h>
#include <yak/util.h>
#include <yak/version.h>
#include <yak/vm/direct.h>
#include <yak/vm/memblock.h>
#include <yak/vm/page.h>
#include <yak/vm/pmm.h>
#include <yak/wait.h>

namespace yak {

constinit GlobalInitEngine init_engine;
CPUDATA_DECL constinit CpuData bsp_cpu_data = CpuData();

/* Declared in the linker script */
extern "C" void (*__init_array_start[])();
extern "C" void (*__init_array_end[])();

void run_init_array() {
  size_t count = __init_array_end - __init_array_start;

  pr_debug("running %zu constructors\n", count);

  for (size_t i = 0; i < count; i++) {
    auto constructor = __init_array_start[i];
    constructor();
  }
}

#if CONFIG_BOOT_BANNER
const char boot_banner[] = {
#embed "banner.txt"
    , '\0'};
#endif

namespace arch {
bool is_canonical(uintptr_t addr);
}

extern void test_fn();

Thread bsp_idle_thread = Thread("idle_thread0", SchedPrio::Idle,
                                &kernel_process, Thread::Type::IdleThread);

extern "C" void kernel_entry(void *bsp_idle_stack_top) {
  arch::early_output_init();

  printk(LogLevel::Raw, "\e[0m");

#if CONFIG_BOOT_BANNER
  pr_info("%s", boot_banner);
#endif
  pr_info("Booting Yak/" ARCH_STR " v" KERNEL_VERSION_STR
          " (commit: " KERNEL_GIT_HASH ")\n");

#ifndef YAK_HOSTED_MODE
  run_init_array();
#endif

  CpuData::initialize(&bsp_cpu_data);
  bsp_cpu_data.bsp = true;

  arch::early_init();

  bsp_idle_thread.kernel_stack_top = bsp_idle_stack_top;
  bsp_cpu_data.bootstrap_scheduler(&bsp_idle_thread);

  // We now have defined thread-state;
  // Sleeping locks are usable.

  arch::mem_init();

  arch::boot_finalize();

  boot_memblock.done();

  arch::post_pmm();

  Event ev = Event(false, Event::Type::Notify);

  auto pg_stack1 = expect(pmm_alloc(0, PageUse::Wired, ALLOC_ZERO), "oom");
  auto pg_stack_win1 =
      expect(MapWindow::create(pg_stack1->to_pa(), 4096), "could not map");
  auto pg_stack2 = expect(pmm_alloc(0, PageUse::Wired, ALLOC_ZERO), "oom");
  auto pg_stack_win2 =
      expect(MapWindow::create(pg_stack2->to_pa(), 4096), "could not map");

  auto t1 = Thread("test1", SchedPrio::RealTime, &kernel_process);
  auto t2 = Thread("test2", SchedPrio::RealTime, &kernel_process);

  t1.init_context((void *) ((vaddr_t) pg_stack_win1.data() + 4096),
                  [](void *obj, void *n) {
                    Thread::current()->set_priority(SchedPrio::RealTime + 2);
                    pr_debug("enter t@%zu! (%p)\n", (uint64_t) n, obj);
                    auto &o = *(Event *) obj;
                    KObject *objs[] = {&o};
                    auto res =
                        wait_for_many(objs, WaitMode::Block, WaitType::Any);
                    pr_debug("acq ok? %s\n", res.has_value() ? "yes" : "no");

                    // o.alarm();
                  },
                  &ev, (void *) 1);

  t2.init_context((void *) ((vaddr_t) pg_stack_win2.data() + 4096),
                  [](void *obj, void *n) {
                    Thread::current()->set_priority(SchedPrio::RealTime + 1);
                    pr_debug("enter t@%zu! (%p)\n", (uint64_t) n, obj);
                    auto &o = *(Event *) obj;
                    auto res = wait_for_single(o, WaitMode::Block);
                    pr_debug("acq ok? %s\n", res.has_value() ? "yes" : "no");

                    // o.alarm();
                  },
                  &ev, (void *) 2);

  pr_debug("begin resumes\n");

  CpuData::local_scheduler().resume(&t1);
  CpuData::local_scheduler().resume(&t2);

  pr_debug("run next\n");

  ev.alarm();
  ev.clear();

  // XXX: rather run this on the kmain thread!
  init_engine.run();

  auto buf_pg = expect(pmm_alloc(2, PageUse::Wired, 0), "test");
  auto window = expect(MapWindow::create(buf_pg->to_pa(), buf_pg->block_size()),
                       "could not map buf_pg");
  Page **buf_allocs = window.as<Page *>();
  size_t n_allocs = (1 << (2 + 12)) / sizeof(Page *);
  pr_debug("buf_allocs:%p\n", buf_allocs);
  pr_debug("n_allocs:%zu\n", n_allocs);

  for (size_t i = 0; i < n_allocs; i++) {
    buf_allocs[i] = expect(pmm_alloc(1, PageUse::Wired, ALLOC_ZERO), "oom :(");
  }

  for (size_t i = 0; i < n_allocs; i++) {
    buf_allocs[i]->release();
    buf_allocs[i] = nullptr;
  }

  buf_pg->release();

  auto fib = [](this auto self, int n) -> int {
    return n <= 1 ? n : self(n - 1) + self(n - 2);
  };
  pr_debug("deduced \"this\" test: fib(10)=%d\n", fib(10));

  idle_loop();
}

} // namespace yak
