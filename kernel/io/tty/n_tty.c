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

static void commit_line(struct tty *tty)
{
	if (tty->canonical_pos == 0)
		return;

	ringbuffer_put(&tty->read_buf, tty->canonical_buf, tty->canonical_pos);

	tty->canonical_pos = 0;

	event_alarm(&tty->vnode->poll_event, true);
	event_alarm(&tty->data_available, false);
}

static void do_echo(struct tty *tty, char c)
{
	struct termios *t = &tty->termios;

	if (!(t->c_lflag & ECHO)) {
		if (c == '\n' && (t->c_lflag & ECHONL)) {
			tty->driver_ops->write(tty, &c, 1);
		}
		return;
	}

	tty->driver_ops->write(tty, &c, 1);
}

static void receive_char(struct tty *tty, char c)
{
	guard(mutex)(&tty->read_mutex);

	struct termios *t = &tty->termios;

	if (t->c_lflag & ICANON) {
		// Backspace
		if (c == t->c_cc[VERASE]) {
			if (tty->canonical_pos > 0) {
				tty->canonical_pos--;
				if (t->c_lflag & ECHO)
					tty->driver_ops->write(tty, "\b \b", 3);
			}
			return;
		}

		// Line Delete
		if (c == t->c_cc[VKILL]) {
			if (t->c_lflag & ECHO) {
				while (tty->canonical_pos > 0) {
					tty->canonical_pos--;
					tty->driver_ops->write(tty, "\b \b", 3);
				}
			} else {
				tty->canonical_pos = 0;
			}
			return;
		}

		// EOF (ctrl+d)
		if (c == t->c_cc[VEOF]) {
			commit_line(tty);
			return;
		}

		if (tty->canonical_pos < TTY_BUF_SIZE - 1) {
			tty->canonical_buf[tty->canonical_pos++] = c;
		}

		do_echo(tty, c);

		if (c == '\n') {
			commit_line(tty);
		}

		return;
	}

	// Non-canonical mode
	ringbuffer_put(&tty->read_buf, &c, 1);
	event_alarm(&tty->vnode->poll_event, true);
	event_alarm(&tty->data_available, false);
}

static size_t check_line(struct ringbuffer *rb)
{
	size_t avail = ringbuffer_available(rb);

	for (size_t i = 0; i < avail; i++) {
		size_t buf_idx = (rb->tail + i) % rb->capacity;
		char c = rb->data[buf_idx];
		if (c == '\n')
			return i + 1;
	}
	return 0;
}

static status_t read(struct tty *tty, char *buf, size_t len, size_t *read_bytes)
{
	struct termios *t = &tty->termios;

	size_t got = 0;

	kmutex_acquire(&tty->read_mutex, TIMEOUT_INFINITE);

	if (t->c_lflag & ICANON) {
		while (1) {
			size_t n = check_line(&tty->read_buf);
			if (n == 0 &&
			    ringbuffer_available(&tty->read_buf) > 0) {
				// a partial line exists
				// (i.e. Ctrl+D)
				n = ringbuffer_available(&tty->read_buf);
			}

			if (n > 0) {
				got = ringbuffer_get(&tty->read_buf, buf,
						     MIN(n, len));
				break;
			}

			event_clear(&tty->data_available);

			kmutex_release(&tty->read_mutex);

			EXPECT(sched_wait(&tty->data_available, WAIT_MODE_BLOCK,
					  TIMEOUT_INFINITE));

			kmutex_acquire(&tty->read_mutex, TIMEOUT_INFINITE);
		}
	} else {
		cc_t vmin = t->c_cc[VMIN];
		cc_t vtime = t->c_cc[VTIME];

		nstime_t timeout = TIMEOUT_INFINITE;
		bool first_byte = true;

		while (got == 0 || (vmin > 0 && got < vmin)) {
			size_t avail = ringbuffer_available(&tty->read_buf);
			if (avail > 0) {
				size_t take = MIN(avail, len - got);
				got += ringbuffer_get(&tty->read_buf, buf + got,
						      take);

				if (vmin == 0)
					break;

				first_byte = false;
				continue;
			}

			if (vtime > 0) {
				// vtime is measuerd in 0.1 second steps
				// so convert to ns first
				timeout = vtime * 100000000L;
				if (!first_byte && vmin > 0)
					timeout = vtime * 100000000L;
			}

			event_clear(&tty->data_available);
			kmutex_release(&tty->read_mutex);

			status_t res = sched_wait(&tty->data_available,
						  WAIT_MODE_BLOCK, timeout);

			kmutex_acquire(&tty->read_mutex, TIMEOUT_INFINITE);

			if (IS_ERR(res)) {
				break;
			}

			if (vtime > 0 && !first_byte &&
			    ringbuffer_available(&tty->read_buf) == 0)
				break;
		}
	}

	if (ringbuffer_available(&tty->read_buf) == 0)
		event_clear(&tty->data_available);

	kmutex_release(&tty->read_mutex);

	*read_bytes = got;
	return YAK_SUCCESS;
}

static status_t write(struct tty *tty, const char *buf, size_t len,
		      size_t *written_bytes)
{
	struct termios *t = &tty->termios;
	for (size_t i = 0; i < len; i++) {
		char c = buf[i];

		if (t->c_oflag & OPOST) {
			if (c == '\n' && (t->c_oflag & ONLCR)) {
				tty->driver_ops->write(tty, "\r\n", 2);
				continue;
			}
			if (c == '\r' && (t->c_oflag & OCRNL)) {
				c = '\n';
			}
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

		if (tty->termios.c_lflag & ICANON) {
			if (check_line(&tty->read_buf) > 0)
				*ret |= POLLIN;
		} else {
			if (ringbuffer_available(&tty->read_buf) > 0)
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
