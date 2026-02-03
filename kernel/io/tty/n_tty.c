#include "yak-abi/poll.h"
#include <string.h>
#include <yak-abi/termios.h>
#include <yak-abi/ioctls.h>
#include <yak/mutex.h>
#include <yak/ringbuffer.h>
#include <yak/sched.h>
#include <yak/status.h>
#include <yak/wait.h>
#include <yak/types.h>
#include <yak/tty.h>

#include <yak/log.h>

static void receive_char(struct tty *tty, char c)
{
	struct termios *t = &tty->termios;

	bool echo = (t->c_lflag & ECHO);

	if (t->c_lflag & ICANON) {
		if (c == t->c_cc[VERASE]) {
			if (tty->canonical_pos > 0) {
				tty->canonical_pos--;
				if (echo)
					tty->driver_ops->write(tty, "\b \b", 3);
			}
			return;
		}

		if (c == t->c_cc[VKILL]) {
			if (echo) {
				while (tty->canonical_pos-- > 0)
					tty->driver_ops->write(tty, "\b \b", 3);
			}
			tty->canonical_pos = 0;
			return;
		}

		if (tty->canonical_pos < TTY_BUF_SIZE - 1) {
			tty->canonical_buf[tty->canonical_pos++] = c;
			if (echo)
				tty->driver_ops->write(tty, &c, 1);
		}

		if (c == '\n' || c == t->c_cc[VEOF]) {
			guard(mutex)(&tty->read_mutex);
			ringbuffer_put(&tty->read_buf, tty->canonical_buf,
				       tty->canonical_pos);
			event_alarm(&tty->vnode->poll_event, true);
			event_alarm(&tty->data_available, false);
		}

		return;
	}

	guard(mutex)(&tty->read_mutex);
	ringbuffer_put(&tty->read_buf, &c, 1);
	event_alarm(&tty->vnode->poll_event, true);
	event_alarm(&tty->data_available, false);
}

static size_t check_line(struct ringbuffer *rb, struct termios *t)
{
	for (size_t i = 0; i < ringbuffer_available(rb); i++) {
		size_t idx = (rb->tail + i) % rb->capacity;
		char c = rb->data[idx];
		if (c == '\n' || c == t->c_cc[VEOF])
			return i + 1;
	}
	return 0;
}

static status_t read(struct tty *tty, char *buf, size_t len, size_t *read_bytes)
{
	struct termios *t = &tty->termios;

	kmutex_acquire(&tty->read_mutex, TIMEOUT_INFINITE);

	if (t->c_lflag & ICANON) {
		size_t n;
		while (1) {
			n = check_line(&tty->read_buf, t);
			if (n > 0)
				break;

			kmutex_release(&tty->read_mutex);

			EXPECT(sched_wait(&tty->data_available, WAIT_MODE_BLOCK,
					  TIMEOUT_INFINITE));

			kmutex_acquire(&tty->read_mutex, TIMEOUT_INFINITE);
		}
		*read_bytes = ringbuffer_get(&tty->read_buf, buf, MIN(n, len));
	} else {
		if (t->c_cc[VMIN] == 0 && t->c_cc[VTIME] == 0) {
			*read_bytes = ringbuffer_get(&tty->read_buf, buf, len);
		} else {
			while (ringbuffer_available(&tty->read_buf) <
			       t->c_cc[VMIN]) {
				nstime_t tm = TIMEOUT_INFINITE;

				if (t->c_cc[VTIME] > 0) {
					tm = t->c_cc[VTIME] * 100000000L;
				}

				kmutex_release(&tty->read_mutex);

				status_t res = sched_wait(&tty->data_available,
							  WAIT_MODE_BLOCK, tm);

				if (IS_ERR(res)) {
					return res;
				}

				kmutex_acquire(&tty->read_mutex,
					       TIMEOUT_INFINITE);
			}
		}

		*read_bytes = ringbuffer_get(&tty->read_buf, buf, len);
	}

	if (ringbuffer_available(&tty->read_buf) == 0) {
		event_clear(&tty->data_available);
	}

	kmutex_release(&tty->read_mutex);
	return YAK_SUCCESS;
}

static status_t write(struct tty *tty, const char *buf, size_t len,
		      size_t *written_bytes)
{
	struct termios *t = &tty->termios;
	for (size_t i = 0; i < len; i++) {
		char c = buf[i];
		if (c == '\n') {
			if (t->c_oflag & ONLCR)
				tty->driver_ops->write(tty, "\r", 1);
		} else if (c == '\r') {
			if (t->c_oflag & OCRNL)
				c = '\n';
		}
		tty->driver_ops->write(tty, &c, 1);
	}

	*written_bytes = len;
	return YAK_SUCCESS;
}

static void flush(struct tty *tty)
{
	if (tty->driver_ops->flush)
		tty->driver_ops->flush(tty);
}

static status_t ioctl(struct tty *tty, unsigned long com, void *data, int *ret)
{
	switch (com) {
	case TCGETS: {
		struct termios *arg = data;
		memcpy(arg, &tty->termios, sizeof(struct termios));
		break;
	}
	case TCSETS: {
		const struct termios *arg = data;
		memcpy(&tty->termios, arg, sizeof(struct termios));
		tty->driver_ops->set_termios(tty, &tty->termios);
		break;
	}
	default:
		return YAK_NOTTY;
	}

	return YAK_SUCCESS;
}

static status_t poll(struct tty *tty, short mask, short *ret)
{
	if (mask & POLLOUT)
		*ret |= POLLOUT;

	if (mask & POLLIN) {
		guard(mutex)(&tty->read_mutex);
		if (ringbuffer_available(&tty->read_buf)) {
			*ret |= POLLIN;
		}
	}

	return YAK_SUCCESS;
}

struct tty_ldisc_ops n_tty_ldisc = {
	.receive_char = receive_char,
	.write = write,
	.read = read,
	.flush = flush,
	.ioctl = ioctl,
	.poll = poll,
};

struct tty_ldisc_ops *get_n_tty_ldisc()
{
	return &n_tty_ldisc;
}
