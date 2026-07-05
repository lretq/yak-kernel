#include <atomic>
#include <string.h>
#include <yak/cpudata.h>

namespace yak {
static std::atomic<size_t> next_id = 0;

// Initialize everything except for the scheduler (and directly adjacent fields)
void CpuData::initialize(CpuData *data) {
  memset(data, 0, sizeof(CpuData));

  data->self = data;

  data->id = next_id.fetch_add(1, std::memory_order_relaxed);

  data->numa_domain = 0;
  data->bsp = false;

  data->kernel_stack_top = nullptr;
  data->current_thread = nullptr;

  data->dpc_queue.initialize();

  data->md.md_init();
}

void CpuData::bootstrap_scheduler(Thread *idle_thread) {
  idle_thread->state = ThreadState::Running;
  idle_thread->current_cpu = this;

  current_thread = idle_thread;
  kernel_stack_top = idle_thread->kernel_stack_top;

  sched.initialize(this, idle_thread);
}

} // namespace yak
