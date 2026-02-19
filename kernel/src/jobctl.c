#include <assert.h>
#include <yak/heap.h>
#include <yak/status.h>
#include <yak/queue.h>
#include <yak/spinlock.h>
#include <yak/refcount.h>
#include <yak/process.h>

// XXX: slabs
static struct session *alloc_session()
{
	return kzalloc(sizeof(struct session));
}

static struct pgrp *alloc_pgrp()
{
	return kzalloc(sizeof(struct pgrp));
}

static void free_session(struct session *session)
{
	kfree(session, 0);
}

static void free_pgrp(struct pgrp *pgrp)
{
	kfree(pgrp, 0);
}

static void session_cleanup(struct session *session)
{
	free_session(session);
	panic("session todo");
}

static void pgrp_cleanup(struct pgrp *pgrp)
{
	free_pgrp(pgrp);
	panic("pgrp todo");
}

GENERATE_REFMAINT(session, refcount, session_cleanup);
GENERATE_REFMAINT(pgrp, refcount, pgrp_cleanup);

pid_t process_getpgid(struct kprocess *process)
{
	ipl_t ipl = spinlock_lock(&process->jobctl_lock);
	pid_t id = process->pgrp->pgid;
	spinlock_unlock(&process->jobctl_lock, ipl);
	return id;
}

pid_t process_getsid(struct kprocess *process)
{
	ipl_t ipl = spinlock_lock(&process->jobctl_lock);
	pid_t id = process->session->sid;
	spinlock_unlock(&process->jobctl_lock, ipl);
	return id;
}

void pgrp_insert(struct pgrp *pgrp, struct kprocess *process)
{
	assert(process->pgrp == NULL);
	assert(spinlock_held(&process->jobctl_lock));

	pgrp_ref(pgrp);
	process->pgrp = pgrp;

	ipl_t ipl = spinlock_lock(&pgrp->pgrp_lock);
	LIST_INSERT_HEAD(&pgrp->members, process, pgrp_entry);
	spinlock_unlock(&pgrp->pgrp_lock, ipl);
}

void session_insert(struct session *session, struct kprocess *process)
{
	assert(process->session == NULL);
	assert(spinlock_held(&process->jobctl_lock));

	session_ref(session);

	ipl_t ipl = spinlock_lock(&session->session_lock);
	LIST_INSERT_HEAD(&session->members, process, session_entry);
	process->session = session;
	spinlock_unlock(&session->session_lock, ipl);
}

void session_remove(struct kprocess *process)
{
	assert(process->session);
	assert(spinlock_held(&process->jobctl_lock));

	struct session *session = process->session;

	ipl_t ipl = spinlock_lock(&session->session_lock);
	LIST_REMOVE(process, session_entry);
	spinlock_unlock(&session->session_lock, ipl);

	// drop the reference
	process->session = NULL;
	session_deref(session);
}

void pgrp_remove(struct kprocess *process)
{
	assert(process->pgrp);
	assert(spinlock_held(&process->jobctl_lock));

	struct pgrp *pgrp = process->pgrp;

	ipl_t ipl = spinlock_lock(&pgrp->pgrp_lock);
	LIST_REMOVE(process, pgrp_entry);
	spinlock_unlock(&pgrp->pgrp_lock, ipl);

	// drop the reference
	process->pgrp = NULL;
	pgrp_deref(pgrp);
}

struct pgrp *pgrp_create(struct session *session, struct kprocess *leader)
{
	assert(spinlock_held(&leader->jobctl_lock));

	struct pgrp *pgrp = alloc_pgrp();
	if (!pgrp)
		return NULL;

	pgrp->pgid = leader->pid;
	pgrp->refcount = 1;
	pgrp->session = session;
	LIST_INIT(&pgrp->members);
	spinlock_init(&pgrp->pgrp_lock);

	ipl_t ipl = spinlock_lock(&session->session_lock);
	session_ref(session);
	LIST_INSERT_HEAD(&session->pgrps, pgrp, list_entry);
	spinlock_unlock(&session->session_lock, ipl);

	return pgrp;
}

struct session *session_create(struct kprocess *leader)
{
	assert(spinlock_held(&leader->jobctl_lock));

	struct session *session = alloc_session();
	if (!session)
		return NULL;

	session->sid = leader->pid;
	session->refcount = 1;
	session->ctty = NULL;
	session->had_ctty = false;
	LIST_INIT(&session->members);
	LIST_INIT(&session->pgrps);
	spinlock_init(&session->session_lock);

	return session;
}

status_t jobctl_setsid(struct kprocess *process)
{
	ipl_t ipl = spinlock_lock(&process->jobctl_lock);

	// check if calling proc is already a leader
	// session may be NULL only if we launch a process with the kernel as parent
	if (process->session != NULL &&
	    (process->session->sid == process->pid ||
	     process->pgrp->pgid == process->pid)) {
		spinlock_unlock(&process->jobctl_lock, ipl);
		return YAK_PERM_DENIED;
	}

	assert(process->session != NULL || process->ppid == 0);

	struct session *new_session = session_create(process);
	if (!new_session) {
		spinlock_unlock(&process->jobctl_lock, ipl);
		return YAK_OOM;
	}

	struct pgrp *new_pgrp = pgrp_create(new_session, process);
	if (!new_pgrp) {
		session_deref(new_session);
		spinlock_unlock(&process->jobctl_lock, ipl);
		return YAK_OOM;
	}

	if (process->pgrp) {
		pgrp_remove(process);
	}

	if (process->session) {
		session_remove(process);
	}

	session_insert(new_session, process);
	pgrp_insert(new_pgrp, process);

	insert_sid(new_session);
	insert_pgrp(new_pgrp);

	// finally release the temporary references
	session_deref(new_session);
	pgrp_deref(new_pgrp);

	spinlock_unlock(&process->jobctl_lock, ipl);
	return YAK_SUCCESS;
}

status_t jobctl_setpgid(struct kprocess *proc, pid_t pgid)
{
	ipl_t ipl = spinlock_lock(&proc->jobctl_lock);

	struct pgrp *pgrp = NULL;
	if (proc->session->sid == proc->pid) {
		// a session leader cannot change it's pgrp
		pgrp = NULL;
	} else if (pgid == 0 || pgid == proc->pid) {
		pgid = proc->pid;
		pgrp = pgrp_create(proc->session, proc);
	} else {
		pgrp = lookup_pgid(pgid);
		if (pgrp && pgrp->session != proc->session)
			pgrp = NULL;
	}

	if (pgrp == NULL) {
		spinlock_unlock(&proc->jobctl_lock, ipl);
		return YAK_PERM_DENIED;
	}

	if (proc->pgrp) {
		pgrp_remove(proc);
	}

	pgrp_insert(pgrp, proc);

	spinlock_unlock(&proc->jobctl_lock, ipl);
	return YAK_SUCCESS;
}
