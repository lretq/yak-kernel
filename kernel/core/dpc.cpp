#include <yak/assert.h>
#include <frg/mutex.hpp>
#include <yak/cpudata.h>
#include <yak/dpc.h>
#include <yak/interrupt-guard.h>
#include <yak/ipl-guard.h>
#include <yak/ipl.h>
#include <yak/panic.h>
#include <yak/softint.h>

namespace yak {
void Dpc::init(DpcCallback callback) {
  assert(enqueued_to_ == nullptr);
  callback_ = callback;
  context_ = nullptr;
}

void DpcQueue::enqueue(Dpc *dpc, void *context) {
  InterruptGuard ints{};

  auto dpc_queue = CpuData::current()->dpc_queue.get();
  auto guard = frg::guard(&dpc_queue->queue_lock);

  // already enqueued
  if (dpc->enqueued_to_ != nullptr)
    return;

  dpc->enqueued_to_ = dpc_queue;
  dpc->context_ = context;

  dpc_queue->queue.push_back(dpc);

  softint_issue(Ipl::dispatch);
}

void Dpc::dequeue() {
  InterruptGuard ints{};

  auto q = enqueued_to_;

  if (q == nullptr)
    return;

  auto guard = frg::guard(&q->queue_lock);
  q->queue.erase(q->queue.iterator_to(this));

  enqueued_to_ = nullptr;
}

void DpcQueue::drain() {
  assert(iplget() == Ipl::dispatch);

  do {
    InterruptGuard ints{};
    auto guard = frg::guard(&queue_lock);
    if (queue.empty()) {
      return;
    }

    auto captured_dpc = queue.pop_front();
    auto captured_context = captured_dpc->context_;

    captured_dpc->enqueued_to_ = nullptr;

    guard.unlock();
    ints.unlock();

    captured_dpc->callback_(captured_dpc, captured_context);
  } while (true);
}

} // namespace yak
