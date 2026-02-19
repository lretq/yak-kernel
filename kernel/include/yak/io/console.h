#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <yak/queue.h>
#include <yak-abi/termios.h>

struct console {
	char name[32];

	size_t (*write)(struct console *console, const char *buffer,
			size_t length);

	void *private;

	struct winsize native_winsize;

	TAILQ_ENTRY(console) console_list_entry;
	TAILQ_ENTRY(console) sink_list_entry;
};

typedef TAILQ_HEAD(console_list, console) console_list_t;

void console_register(struct console *console);
void sink_add(struct console *console);
void sink_remove(struct console *console);
void sink_foreach(void (*cb)(struct console *, void *), void *private);

// setup console tty
void console_init();

#ifdef __cplusplus
}
#endif
