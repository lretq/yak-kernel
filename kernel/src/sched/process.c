#include <yak/file.h>
#include <yak/vm/map.h>
#include <yak/queue.h>
#include <yak/semaphore.h>
#include <yak/heap.h>
#include <yak/init.h>
#include <yak/log.h>
#include <yak/status.h>
#include <yak/jobctl.h>
#include <stdint.h>
#include <string.h>
#include <yak/process.h>
#include <yak/mutex.h>
#include <yak/hashtable.h>
#include <yak/cpudata.h>
#include <yak/spinlock.h>

struct kprocess kproc0;

static uint64_t next_pid = 0;

struct id_map {
	struct spinlock table_lock;
	hashtable_t table;
};

struct id_map pid_table;
struct id_map sid_table;
struct id_map pgid_table;

// for now: hash(pid) = pid
static hash_t hash_id(const void *key, [[maybe_unused]] const size_t key_len)
{
	return *(const pid_t *)key;
}

static bool eq_id(const void *a, const void *b,
		  [[maybe_unused]] const size_t key_len)
{
	return *(const pid_t *)a == *(const pid_t *)b;
}

static void id_map_init(struct id_map *t)
{
	spinlock_init(&t->table_lock);
	ht_init(&t->table, hash_id, eq_id);
}

static status_t id_map_push(struct id_map *t, pid_t id, void *p)
{
	bool state = spinlock_lock_interrupts(&t->table_lock);
	EXPECT(ht_set(&t->table, (void *)&id, sizeof(pid_t), p, 0));
	spinlock_unlock_interrupts(&t->table_lock, state);
	return YAK_SUCCESS;
}

static status_t id_map_remove(struct id_map *t, pid_t id)
{
	bool state = spinlock_lock_interrupts(&t->table_lock);
	ht_del(&t->table, (void *)&id, sizeof(pid_t));
	spinlock_unlock_interrupts(&t->table_lock, state);
	return YAK_NOENT;
}

static void *id_map_find(struct id_map *t, pid_t pid)
{
	bool state = spinlock_lock_interrupts(&t->table_lock);
	void *val = ht_get(&t->table, (void *)&pid, sizeof(pid_t));
	spinlock_unlock_interrupts(&t->table_lock, state);
	return val;
}

void insert_sid(struct session *session)
{
	id_map_push(&sid_table, session->sid, session);
}

void insert_pgrp(struct pgrp *pgrp)
{
	id_map_push(&pgid_table, pgrp->pgid, pgrp);
}

struct kprocess *lookup_pid(pid_t pid)
{
	return id_map_find(&pid_table, pid);
}

struct session *lookup_sid(pid_t sid)
{
	return id_map_find(&sid_table, sid);
}

struct pgrp *lookup_pgid(pid_t pgid)
{
	return id_map_find(&pgid_table, pgid);
}

void kprocess_init(struct kprocess *process)
{
	memset(process, 0, sizeof(struct kprocess));

	process->state = PROC_ALIVE;
	process->exit_status = 0;
	semaphore_init(&process->wait_semaphore, 0);

	process->pid = __atomic_fetch_add(&next_pid, 1, __ATOMIC_SEQ_CST);
	process->parent_process = NULL;

	kmutex_init(&process->child_list_lock, "proc_child_list");
	LIST_INIT(&process->child_list);

	spinlock_init(&process->thread_list_lock);
	LIST_INIT(&process->thread_list);
	process->thread_count = 0;

	if (likely(process != &kproc0)) {
		process->map = kzalloc(sizeof(struct vm_map));
		vm_map_init(process->map);
	} else {
		process->map = kmap();
	}
}

void uprocess_init(struct kprocess *process, struct kprocess *parent)
{
	kprocess_init(process);

	process->ppid = parent->pid;
	process->parent_process = parent;

	kmutex_init(&process->fd_mutex, "fd");
	process->fd_cap = 0;
	process->fds = NULL;

	spinlock_init(&process->jobctl_lock);
	process->session = NULL;
	process->pgrp = NULL;

	spinlock_init(&process->fs_lock);
	process->cwd = NULL;
	process_setcwd(process, vfs_getroot());

	id_map_push(&pid_table, process->pid, process);

	guard(mutex)(&parent->child_list_lock);
	LIST_INSERT_HEAD(&parent->child_list, process, child_list_entry);
}

void proc_init()
{
	id_map_init(&pid_table);
	id_map_init(&sid_table);
	id_map_init(&pgid_table);
}

struct vnode *process_getcwd(struct kprocess *proc)
{
	ipl_t ipl = spinlock_lock(&proc->fs_lock);
	struct vnode *vn = curproc()->cwd;
	assert(vn);
	vnode_ref(vn);
	spinlock_unlock(&proc->fs_lock, ipl);
	return vn;
}

void process_setcwd(struct kprocess *proc, struct vnode *vn)
{
	ipl_t ipl = spinlock_lock(&proc->fs_lock);
	struct vnode *old_cwd = proc->cwd;
	proc->cwd = vn;
	spinlock_unlock(&proc->fs_lock, ipl);
	if (old_cwd)
		vnode_deref(old_cwd);
}

void process_set_exit_status(struct kprocess *proc, int status)
{
	// don't overwrite the exit status (e.g. we exit and handle a SIGKILL concurrently)
	if (!__atomic_test_and_set(&proc->is_exiting, __ATOMIC_ACQ_REL)) {
		__atomic_store_n(&proc->exit_status, status, __ATOMIC_RELEASE);
	}
}

void process_destroy(struct kprocess *process)
{
	if (process->pid == 0 || process->pid == 1)
		panic("try to destroy init or kernel\n");

	pr_warn("implement full process destruction!\n");

	// biggest memory hog; get rid of this first
	vm_map_destroy(process->map);

	ipl_t ipl = spinlock_lock(&process->jobctl_lock);
	pgrp_remove(process);
	session_remove(process);
	spinlock_unlock(&process->jobctl_lock, ipl);

	{
		struct kprocess *init_process = lookup_pid(1);
		assert(init_process);

		guard(mutex)(&process->child_list_lock);
		while (!LIST_EMPTY(&process->child_list)) {
			struct kprocess *child =
				LIST_FIRST(&process->child_list);
			LIST_REMOVE(child, child_list_entry);

			ipl_t ipl = spinlock_lock(&child->thread_list_lock);
			__atomic_store_n(&child->ppid, 1, __ATOMIC_RELEASE);
			child->parent_process = init_process;
			spinlock_unlock(&child->thread_list_lock, ipl);

			guard(mutex)(&init_process->child_list_lock);
			LIST_INSERT_HEAD(&init_process->child_list, child,
					 child_list_entry);
		}
	}

	// Remove from pid->process map
	id_map_remove(&pid_table, process->pid);

	assert(LIST_EMPTY(&process->child_list));

	{
		guard(mutex)(&process->fd_mutex);
		for (int i = 0; i < process->fd_cap; i++) {
			struct fd *fd = process->fds[i];
			if (fd != NULL) {
				fd_close(process, i);
			}
		}
		kfree(process->fds, process->fd_cap * sizeof(struct fd *));
	}

	vnode_deref(process->cwd);
	process->cwd = NULL;
}

INIT_DEPS(proc);
INIT_ENTAILS(proc);
INIT_NODE(proc, proc_init);
