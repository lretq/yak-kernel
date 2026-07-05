#pragma once

namespace yak {
class Thread;
}

namespace yak::arch {
void early_output_init();
void early_init();
void mem_init();
void boot_finalize();
void post_pmm();

void sched_switch(Thread *current, Thread *thread);
} // namespace yak::arch
