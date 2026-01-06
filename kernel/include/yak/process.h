#pragma once

#include <stddef.h>
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

struct kprocess {
	pid_t pid;

	pid_t ppid;
	// cached pointer
	struct kprocess *parent_process;

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
