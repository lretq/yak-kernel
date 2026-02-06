#include <yak/process.h>
#include <yak/queue.h>
#include <yak/jobctl.h>
#include <yak/spinlock.h>
#include <yak/types.h>
#include <yak/log.h>
#include <yak/cpudata.h>
#include <yak/sched.h>
#include <yak/syscall.h>
#include <yak/timespec.h>
#include <yak-abi/errno.h>
#include <yak-abi/syscall.h>

DEFINE_SYSCALL(SYS_EXIT, exit, int rc)
{
	struct kprocess *proc = curproc();
	process_set_exit_status(proc, EXIT_STATUS(rc));
	pr_warn("sys_exit: we have to kill our sibling threads (pid=%llu)\n",
		proc->pid);
	sched_exit_self();
}

DEFINE_SYSCALL(SYS_GETPID, getpid)
{
	return SYS_OK(curproc()->pid);
}

DEFINE_SYSCALL(SYS_GETPPID, getppid)
{
	pid_t ppid = __atomic_load_n(&curproc()->ppid, __ATOMIC_ACQUIRE);
	return SYS_OK(ppid);
}

DEFINE_SYSCALL(SYS_SETSID, setsid)
{
	struct kprocess *proc = curproc();
	status_t rv = jobctl_setsid(proc);
	RET_ERRNO_ON_ERR(rv);
	return SYS_OK(proc->session->sid);
}

DEFINE_SYSCALL(SYS_SETPGID, setpgid, pid_t pid, pid_t pgid)
{
	if (pgid < 0) {
		return SYS_ERR(EINVAL);
	}

	struct kprocess *cur_proc = curproc();

	struct kprocess *proc = NULL;

	if (pid == 0) {
		proc = cur_proc;
	} else {
		proc = lookup_pid(pid);
		if (proc == NULL)
			return SYS_ERR(ESRCH);
	}

	if (proc != cur_proc && proc->parent_process != cur_proc) {
		return SYS_ERR(ESRCH);
	}

	jobctl_setpgid(proc, pgid);

	return SYS_OK(0);
}

DEFINE_SYSCALL(SYS_GETPGID, getpgid, pid_t pid)
{
	struct kprocess *proc;
	if (pid == 0) {
		proc = curproc();
	} else {
		proc = lookup_pid(pid);
		if (!proc)
			return SYS_ERR(ESRCH);
	}

	return SYS_OK(process_getpgid(proc));
}

DEFINE_SYSCALL(SYS_GETSID, getsid, pid_t pid)
{
	struct kprocess *proc;
	if (pid == 0) {
		proc = curproc();
	} else {
		proc = lookup_pid(pid);
		if (!proc)
			return SYS_ERR(ESRCH);
	}

	return SYS_OK(process_getsid(proc));
}

DEFINE_SYSCALL(SYS_SLEEP, sleep, struct timespec *req, struct timespec *rem)
{
	nstime_t sleep_ns = STIME(req->tv_sec) + req->tv_nsec;
	nstime_t start = plat_getnanos();
	ksleep(sleep_ns);
	nstime_t time_passed = plat_getnanos() - start;

	if (rem) {
		nstime_t remaining =
			(sleep_ns > time_passed) ? (sleep_ns - time_passed) : 0;
		rem->tv_sec = remaining / STIME(1);
		rem->tv_nsec = remaining % STIME(1);
	}

	return SYS_OK(0);
}
