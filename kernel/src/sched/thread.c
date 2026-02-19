#include <yak/sched.h>
#include <yak/log.h>
#include <yak/heap.h>

static void ensure_reapable(struct kthread *thread)
{
	// ensures that the thread has switched off
	ipl_t ipl = spinlock_lock(&thread->thread_lock);
	spinlock_unlock(&thread->thread_lock, ipl);
}

void kthread_destroy(struct kthread *thread)
{
	ensure_reapable(thread);

	if (thread->status != THREAD_TERMINATING) {
		pr_debug("DESTROY LIVE THREAD %p (%d)\n", thread,
			 thread->status);
	}
	assert(thread->status == THREAD_TERMINATING);

	struct kprocess *process = thread->owner_process;

	ipl_t ipl = spinlock_lock(&process->thread_list_lock);

	LIST_REMOVE(thread, process_entry);

	if (0 ==
	    __atomic_sub_fetch(&process->thread_count, 1, __ATOMIC_ACQ_REL)) {
		pr_debug("no thread left for process pid=%lld\n", process->pid);
		if (process->pid == 1)
			panic("attempted to kill init!\n");

		assert(process->state == PROC_ALIVE);
		process->state = PROC_ZOMBIE;
		semaphore_signal(&process->parent_process->wait_semaphore);
	}

	spinlock_unlock(&process->thread_list_lock, ipl);

#if 1
	vaddr_t stack_base = (vaddr_t)thread->kstack_top - KSTACK_SIZE;
	vm_kfree((void *)stack_base, KSTACK_SIZE);

	kfree(thread, sizeof(struct kthread));
#endif
}

status_t kernel_thread_create(const char *name, unsigned int priority,
			      void *entry, void *context, int instant_launch,
			      struct kthread **out)
{
	struct kthread *thread = kmalloc(sizeof(struct kthread));
	if (!thread)
		return YAK_OOM;

	kthread_init(thread, name, priority, &kproc0, 0);

	vaddr_t stack_addr = (vaddr_t)vm_kalloc(KSTACK_SIZE, 0);

	if (stack_addr == 0) {
		kfree(thread, sizeof(struct kthread));
		return YAK_OOM;
	}

	void *stack_top = (void *)(stack_addr + KSTACK_SIZE);

	kthread_context_init(thread, stack_top, entry, context, NULL);

	if (out != NULL)
		*out = thread;

	if (instant_launch)
		sched_resume(thread);

	return YAK_SUCCESS;
}
