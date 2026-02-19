#include "yak/file.h"
#include <string.h>
#include <assert.h>
#include <yak/syscall.h>
#include <yak/heap.h>
#include <yak/process.h>
#include <yak/cpudata.h>
#include <yak/queue.h>
#include <yak/log.h>
#include <yak/jobctl.h>
#include <yak/vm/map.h>

void _syscall_fork_trampoline();

DEFINE_SYSCALL(SYS_FORK, fork)
{
	pr_extra_debug("sys_fork()\n");
	struct kprocess *cur_proc = curproc();
	struct kthread *cur_thread = curthread();
	struct kprocess *new_proc = kmalloc(sizeof(struct kprocess));
	assert(new_proc);

	uprocess_init(new_proc, cur_proc);

	struct vnode *old_cwd = process_getcwd(cur_proc);
	process_setcwd(new_proc, old_cwd);

	vm_map_fork(cur_proc->map, new_proc->map);

	ipl_t ipl;

	ipl = spinlock_lock(&cur_proc->jobctl_lock);
	struct session *sess = cur_proc->session;
	struct pgrp *pgrp = cur_proc->pgrp;
	spinlock_unlock(&cur_proc->jobctl_lock, ipl);

	ipl = spinlock_lock(&new_proc->jobctl_lock);
	session_insert(sess, new_proc);
	pgrp_insert(pgrp, new_proc);
	spinlock_unlock(&new_proc->jobctl_lock, ipl);

	kmutex_acquire(&cur_proc->fd_mutex, TIMEOUT_INFINITE);
	fd_clone(cur_proc, new_proc);
	kmutex_release(&cur_proc->fd_mutex);

	struct kthread *new_thread = kmalloc(sizeof(struct kthread));
	assert(new_thread != NULL);
	kthread_init(new_thread, cur_thread->name, cur_thread->priority,
		     new_proc, 1);
	kthread_context_copy(cur_thread, new_thread);

	// Allocate a new kernel stack for the child
	vaddr_t stack_addr = (vaddr_t)vm_kalloc(KSTACK_SIZE, 0);
	stack_addr += KSTACK_SIZE;
	new_thread->kstack_top = (void *)stack_addr;

	// Make enough space for the syscall frame
	stack_addr -= sizeof(struct syscall_frame);
	assert((stack_addr & 0xF) == 0);

	struct syscall_frame *new_frame = (void *)stack_addr;
	// Copy the caller's syscall frame onto the new proc's stack
	memcpy(new_frame, __syscall_ctx, sizeof(struct syscall_frame));

	// TODO: This assumes the calling convention

	// Child sees pid 0
	CTX_SYS_RETVAL(new_frame) = 0;
	// And also no error
	CTX_SYS_ERR(new_frame) = 0;

	uint64_t *sp = (uint64_t *)stack_addr;
	// Make sure the stack is aligned to 16
	sp -= 2;
	// And let the thread return to the fork trampoline
	*sp = (uint64_t)_syscall_fork_trampoline;

	new_thread->pcb.rsp = (uint64_t)sp;

	sched_resume(new_thread);

	return SYS_OK(new_proc->pid);
}
