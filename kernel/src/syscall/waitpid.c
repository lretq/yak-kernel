#include <yak/wait.h>
#include <yak/types.h>
#include <yak/queue.h>
#include <yak/heap.h>
#include <yak/process.h>
#include <yak/log.h>
#include <yak/syscall.h>
#include <yak/cpudata.h>
#include <yak-abi/errno.h>
#include <yak-abi/syscall.h>

// return immediatly if no child has exited
#define WNOHANG 1

#define KNOWN_OPTIONS (WNOHANG)

static void reap_zombie(struct kprocess *zombie, int *status)
{
	if (status) {
		// XXX: safety
		int exit_status =
			__atomic_load_n(&zombie->exit_status, __ATOMIC_RELAXED);
		*status = exit_status;
	}

	LIST_REMOVE(zombie, child_list_entry);

	process_destroy(zombie);
	kfree(zombie, sizeof(struct kprocess));
}

DEFINE_SYSCALL(SYS_WAITPID, waitpid, pid_t pid, int *status, int flags)
{
	if (flags & ~KNOWN_OPTIONS) {
		pr_warn("sys_waitpid: unknown options\n");
	}

	if (pid < -1 || pid == 0) {
		pr_warn("sys_waitpid: unhandled pid %lld\n", pid);
		return SYS_ERR(EOPNOTSUPP);
	}

	struct kprocess *proc = curproc();

	for (;;) {
		bool found_child = false;

		EXPECT(kmutex_acquire(&proc->child_list_lock,
				      TIMEOUT_INFINITE));

		struct kprocess *child;
		LIST_FOREACH(child, &proc->child_list, child_list_entry)
		{
			if (pid != -1 && child->pid != pid)
				continue;

			found_child = true;

			ipl_t ipl = spinlock_lock(&child->thread_list_lock);
			if (child->state == PROC_ZOMBIE) {
				spinlock_unlock(&child->thread_list_lock, ipl);
				pid_t child_pid = child->pid;
				reap_zombie(child, status);
				kmutex_release(&proc->child_list_lock);
				return SYS_OK(child_pid);
			}
			spinlock_unlock(&child->thread_list_lock, ipl);
		}

		kmutex_release(&proc->child_list_lock);

		if (!found_child)
			return SYS_ERR(ECHILD);

		if (flags & WNOHANG)
			return SYS_OK(0);

		EXPECT(sched_wait(&proc->wait_semaphore, WAIT_MODE_BLOCK,
				  TIMEOUT_INFINITE));
	}
}
