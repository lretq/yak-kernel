#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <yak/semaphore.h>
#include <yak/queue.h>
#include <yak/types.h>
#include <yak/spinlock.h>
#include <yak/file.h>
#include <yak/refcount.h>
#include <yak/fs/vfs.h>
#include <yak/vm/map.h>
#include <yak/tty.h>

typedef LIST_HEAD(proc_list, kprocess) proc_list_t;
typedef LIST_HEAD(pgrp_list, pgrp) pgrp_list_t;
typedef LIST_HEAD(thread_list, kthread) thread_list_t;

struct session {
	pid_t sid;
	refcount_t refcount;

	// Controlling TTY
	// NULL if none
	struct tty *ctty;
	bool had_ctty;

	// all processes in this session
	proc_list_t members;
	pgrp_list_t pgrps;

	struct spinlock session_lock;
};

DECLARE_REFMAINT(session);

struct pgrp {
	pid_t pgid;
	refcount_t refcount;

	struct session *session;

	// all processes in this pgrp
	proc_list_t members;
	LIST_ENTRY(pgrp) list_entry;

	struct spinlock pgrp_lock;
};

DECLARE_REFMAINT(pgrp);

enum kprocess_state {
	PROC_ALIVE,
	PROC_STOPPED,
	PROC_ZOMBIE,
};

#define EXIT_STATUS(code) (((code) & 0xff) << 8)
#define EXIT_SIGNAL(sig) ((sig) & 0x7f)
#define EXIT_COREDUMP 0x80

struct kprocess {
	enum kprocess_state state;
	// protected by the thread list lock
	bool is_exiting;
	int exit_status;
	// multiple children may exit at once, so dont use a kevent
	struct semaphore wait_semaphore;

	pid_t pid;

	pid_t ppid;
	// cached pointer
	// protected by thread_list_lock
	struct kprocess *parent_process;

	struct kmutex child_list_lock;
	proc_list_t child_list;
	LIST_ENTRY(kprocess) child_list_entry;

	int uid, euid;
	int gid, egid;

	struct spinlock thread_list_lock;
	size_t thread_count;
	thread_list_t thread_list;

	struct kmutex fd_mutex;
	int fd_cap;
	struct fd **fds;

	struct vm_map *map;

	struct spinlock jobctl_lock;
	struct session *session;
	struct pgrp *pgrp;

	struct spinlock fs_lock;
	struct vnode *cwd;

	LIST_ENTRY(kprocess) session_entry;
	LIST_ENTRY(kprocess) pgrp_entry;
};

// it's me, hi, im the problem its me
extern struct kprocess kproc0;

// initialize the kernel part of a process
void kprocess_init(struct kprocess *process);

// initialize the kernel+user parts of a process
void uprocess_init(struct kprocess *process, struct kprocess *parent);

void process_destroy(struct kprocess *process);

struct kprocess *lookup_pid(pid_t pid);
struct session *lookup_sid(pid_t sid);
struct pgrp *lookup_pgid(pid_t pgid);

void insert_sid(struct session *session);
void insert_pgrp(struct pgrp *pgrp);

pid_t process_getpgid(struct kprocess *process);
pid_t process_getsid(struct kprocess *process);

struct vnode *process_getcwd(struct kprocess *process);

// transfers reference ownership to process
void process_setcwd(struct kprocess *process, struct vnode *vn);

void process_set_exit_status(struct kprocess *proc, int status);

#ifdef __cplusplus
}
#endif
