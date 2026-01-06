#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <yak/kevent.h>
#include <yak/types.h>
#include <yak/ringbuffer.h>
#include <yak/mutex.h>
#include <yak-abi/termios.h>

struct tty;

struct tty_driver_ops {
	ssize_t (*write)(struct tty *tty, const char *buf, size_t len);
	void (*flush)(struct tty *tty);
	void (*set_termios)(struct tty *tty, const struct termios *newt);
};

struct tty_ldisc_ops {
	void (*receive_char)(struct tty *tty, char c);
	status_t (*read)(struct tty *tty, char *buf, size_t len,
			 size_t *read_bytes);
	status_t (*write)(struct tty *tty, const char *buf, size_t len,
			  size_t *written_bytes);
	void (*flush)(struct tty *tty);
	void (*attach)(struct tty *tty);
	status_t (*ioctl)(struct tty *tty, unsigned long com, void *data,
			  int *ret);
};

// processing flows:
// (1) character input -> tty_input() -> written to read_buf -> ldisc->receive_char()
// (2) user write() -> ldisc->write() -> written to write_buf -> driver_ops->write()
//
// If ICANON is set:
// (1) n_tty ldisc additionally writes to canon_buf

#define TTY_BUF_SIZE 4096

struct tty {
	struct tty_driver_ops *driver_ops;
	struct tty_ldisc_ops *ldisc_ops;
	void *ldisc_data;

	struct vnode *vnode;

	char *name;
	int minor;

	struct kmutex ctty_lock;
	struct kprocess *session;

	struct termios termios;

	struct kevent data_available;

	struct kmutex read_mutex;
	struct ringbuffer read_buf;
	struct kmutex write_mutex;
	struct ringbuffer write_buf;

	char canonical_buf[TTY_BUF_SIZE];
	size_t canonical_pos;

	void *ctx;
};

struct tty_ldisc_ops *get_n_tty_ldisc();

void tty_init();
struct tty *tty_create(const char *name, struct tty_driver_ops *driver_ops,
		       struct tty_ldisc_ops *ldisc_ops);
void tty_input(struct tty *tty, char c);

#ifdef __cplusplus
}
#endif
