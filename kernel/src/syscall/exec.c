#include <string.h>
#include <yak-abi/errno.h>
#include <yak-abi/fcntl.h>
#include <yak/file.h>
#include <yak/heap.h>
#include <yak/sched.h>
#include <yak/cpudata.h>
#include <yak/syscall.h>
#include <yak/process.h>
#include <yak/vm/map.h>

DEFINE_SYSCALL(SYS_EXECVE, execve, const char *user_path, char **user_argv,
	       char **user_envp)
{
	struct kprocess *proc = curproc();
	assert(proc->thread_count == 1);

	// block for the autofree'd resources
	{
		char *path = strdup(user_path);
		if (!path)
			return SYS_ERR(ENOMEM);
		guard(autofree)(path, 0);

		size_t argc = 0;
		while (user_argv[argc] != NULL)
			argc++;

		size_t envc = 0;
		while (user_envp[envc] != NULL)
			envc++;

		char **argv = kzalloc(argc * sizeof(char *) + 1);
		if (!argv)
			return SYS_ERR(ENOMEM);
		guard(autofree)(argv, 0);

		char **envp = kzalloc(envc * sizeof(char *) + 1);
		if (!envp)
			return SYS_ERR(ENOMEM);
		guard(autofree)(envp, 0);

		for (size_t i = 0; i < argc; i++) {
			argv[i] = strdup(user_argv[i]);
		}
		for (size_t i = 0; i < envc; i++) {
			envp[i] = strdup(user_envp[i]);
		}

		argv[argc] = NULL;
		envp[envc] = NULL;

		struct vm_map *orig_map = proc->map;

		struct vm_map *new_map = kzalloc(sizeof(struct vm_map));
		assert(new_map);
		vm_map_init(new_map);

		struct kthread *thread;
		status_t rv = launch_elf(proc, new_map, path,
					 curthread()->priority, argv, envp,
					 &thread);

		if (IS_ERR(rv)) {
			vm_map_destroy(new_map);
			return SYS_ERR(status_errno(rv));
		}

		proc->map = new_map;
		vm_map_activate(new_map);

		vm_map_destroy(orig_map);

		{
			guard(mutex)(&proc->fd_mutex);
			for (int i = 0; i < proc->fd_cap; i++) {
				if (proc->fds[i] &&
				    proc->fds[i]->flags & FD_CLOEXEC)
					fd_close(proc, i);
			}
		}

		sched_resume(thread);
	}

	// Simplest solution: create a new thread and kill yourself
	sched_exit_self();
}
