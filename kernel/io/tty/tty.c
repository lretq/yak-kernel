#include <string.h>
#include <yak/kevent.h>
#include <yak/ringbuffer.h>
#include <yak/tty.h>
#include <yak/cpudata.h>
#include <yak/mutex.h>
#include <yak/vm/kmem.h>
#include <yak/fs/devfs.h>
#include <yak/log.h>
#include <yak/heap.h>
#include <yak-abi/ioctls.h>

#define MAX_TTY 64

static struct kmutex ttys_lock;
static struct tty *ttys[MAX_TTY];

static kmem_cache_t *tty_cache;

status_t ctty_open([[maybe_unused]] int minor, struct vnode **vp)
{
	struct kprocess *proc = curproc();

	ipl_t ipl = spinlock_lock(&proc->session->session_lock);

	struct tty *ctty = proc->session->ctty;
	if (ctty) {
		struct vnode *vn = ctty->vnode;

		spinlock_unlock(&proc->session->session_lock, ipl);

		vnode_ref(vn);
		VOP_LOCK(vn);

		VOP_UNLOCK(*vp);
		vnode_deref(*vp);

		*vp = vn;
		return YAK_SUCCESS;
	}

	spinlock_unlock(&proc->session->session_lock, ipl);
	return YAK_IO;
}

struct device_ops ctty_ops = (struct device_ops){
	.dev_open = ctty_open,
};

void tty_init()
{
	kmutex_init(&ttys_lock, "ttys_lock");
	memset(ttys, 0, sizeof(struct tty *) * MAX_TTY);
	tty_cache = kmem_cache_create("tty", sizeof(struct tty), 0, NULL, NULL,
				      NULL, NULL, NULL, KM_SLEEP);

	struct vnode *vn;
	devfs_register("tty", VCHR, DEV_TTY, MAX_TTY, &ctty_ops, &vn);
}

static const struct termios default_termios =
	(struct termios){ .c_iflag = BRKINT | ICRNL | IMAXBEL | IXON,
			  .c_oflag = OPOST | ONLCR,
			  .c_cflag = CREAD | CS8 | HUPCL,
			  .c_lflag = ECHO | ECHOE | ECHOK | ICANON | ISIG |
				     IEXTEN,
			  .c_cc = {
				  [VINTR] = 0x03,
				  [VQUIT] = 0x1c,
				  [VERASE] = 0x7f,
				  [VKILL] = 0x15,
				  [VEOF] = 0x04,
				  [VSTART] = 0x11,
				  [VSTOP] = 0x13,
				  [VSUSP] = 0x1a,
				  [VREPRINT] = 0x12,
				  [VWERASE] = 0x17,
				  [VLNEXT] = 0x16,
				  [VMIN] = 1,
				  [VTIME] = 0,
			  } };

void tty_input(struct tty *tty, char c)
{
	assert(tty);
	assert(tty->ldisc_ops);
	tty->ldisc_ops->receive_char(tty, c);
}

status_t tty_read(int minor, [[maybe_unused]] voff_t offset, void *buf,
		  size_t length, size_t *read_bytes)
{
	struct tty *tty = ttys[minor];
	return tty->ldisc_ops->read(tty, buf, length, read_bytes);
}

status_t tty_write(int minor, [[maybe_unused]] voff_t offset, const void *buf,
		   size_t length, size_t *written_bytes)
{
	struct tty *tty = ttys[minor];
	return tty->ldisc_ops->write(tty, buf, length, written_bytes);
}

status_t tty_open(int minor, struct vnode **vp)
{
	struct tty *tty = ttys[minor];
	struct kprocess *cur_proc = curproc();

	// if we're the session leader
	if (cur_proc->session->sid == cur_proc->pid) {
		// and still need a ctty
		guard(mutex)(&tty->ctty_lock);
		ipl_t ipl = spinlock_lock(&cur_proc->session->session_lock);
		if (!cur_proc->session->had_ctty) {
			// and the tty has no session
			if (tty->session == NULL) {
				pr_debug("session/pid %lld open ctty\n",
					 cur_proc->pid);
				tty->session = cur_proc;
				cur_proc->session->ctty = tty;
				cur_proc->session->had_ctty = true;
			}
		}
		spinlock_unlock(&cur_proc->session->session_lock, ipl);
	}

	return YAK_SUCCESS;
}

status_t tty_core_ioctl(struct tty *tty, unsigned long com, void *data,
			int *ret)
{
	switch (com) {
	case TIOCGWINSZ:
		struct winsize *wsz = data;
		wsz->ws_row = 80;
		wsz->ws_col = 25;
		wsz->ws_xpixel = 0;
		wsz->ws_ypixel = 0;
		return YAK_SUCCESS;
	case TIOCGPGRP:
	case TIOCSPGRP:
		return YAK_SUCCESS;
	default:
		return YAK_NOTTY;
	}
}

status_t tty_ioctl(int minor, unsigned long com, void *data, int *ret)
{
	struct tty *tty = ttys[minor];
	assert(tty);
	switch (com) {
	case TIOCSCTTY:
	case TIOCNOTTY:
	case TIOCGWINSZ:
	case TIOCSWINSZ:
	case TIOCGPGRP:
	case TIOCSPGRP:
		return tty_core_ioctl(tty, com, data, ret);
	case TCGETS:
	case TCSETS:
		return tty->ldisc_ops->ioctl(tty, com, data, ret);
	default:
		return YAK_NOTTY;
	}
}

struct device_ops tty_ops = (struct device_ops){
	.dev_read = tty_read,
	.dev_write = tty_write,
	.dev_open = tty_open,
	.dev_ioctl = tty_ioctl,
};

struct tty *tty_create(const char *name, struct tty_driver_ops *driver_ops,
		       struct tty_ldisc_ops *ldisc_ops)
{
	struct tty *tty = kmem_cache_alloc(tty_cache, KM_SLEEP);
	if (!tty)
		goto cleanup;

	int minor = -1;

	kmutex_acquire(&ttys_lock, TIMEOUT_INFINITE);
	for (int i = 0; i < MAX_TTY; i++) {
		if (ttys[i] == NULL) {
			minor = i;
			ttys[minor] = tty;
			break;
		}
	}
	kmutex_release(&ttys_lock);

	if (minor == -1)
		goto cleanup;

	pr_debug("allocate TTY minor %d\n", minor);

	tty->minor = minor;

	tty->name = strdup(name);
	if (!tty->name)
		goto cleanup;

	kmutex_init(&tty->ctty_lock, "tty_ctty_lock");
	tty->session = NULL;

	tty->termios = default_termios;

	event_init(&tty->data_available, 0);

	tty->driver_ops = driver_ops;
	if (ldisc_ops == NULL)
		ldisc_ops = get_n_tty_ldisc();
	tty->ldisc_ops = ldisc_ops;

	kmutex_init(&tty->read_mutex, "read_mutex");
	ringbuffer_init(&tty->read_buf, TTY_BUF_SIZE);
	kmutex_init(&tty->write_mutex, "write_mutex");
	ringbuffer_init(&tty->write_buf, TTY_BUF_SIZE);

	memset(tty->canonical_buf, 0, TTY_BUF_SIZE);
	tty->canonical_pos = 0;

	struct vnode *vn;
	devfs_register(tty->name, VCHR, DEV_TTY, minor, &tty_ops, &vn);
	tty->vnode = vn;

	return tty;

cleanup:
	if (tty) {
		if (tty->name)
			kfree(tty->name, 0);

		kmem_cache_free(tty_cache, tty);
	}

	return NULL;
}
