#include <yak/mutex.h>
#include <yak/tty.h>

void console_backend_setup();
void console_backend_write(const void *buf, size_t length);

struct tty *console_tty;
static struct kmutex console_lock;

static ssize_t console_write(struct tty *tty, const char *buf, size_t len)
{
	(void)tty;
	guard(mutex)(&console_lock);
	console_backend_write(buf, len);
	return len;
}

static void set_termios(struct tty *tty, const struct termios *newt)
{
	// NOP
}

static struct tty_driver_ops console_ops = {
	.write = console_write,
	.set_termios = set_termios,
	.flush = NULL,
};

void console_init()
{
	kmutex_init(&console_lock, "console");
	console_backend_setup();
	console_tty = tty_create("console", &console_ops, NULL);
}
