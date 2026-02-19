#include "yak/panic.h"
#include <yak/hint.h>
#include <yak/spinlock.h>
#include <yak/queue.h>
#include <yak/console.h>

static SPINLOCK(sink_list_lock);
static struct console_list sink_list = TAILQ_HEAD_INITIALIZER(sink_list);

static SPINLOCK(console_list_lock);
static struct console_list console_list = TAILQ_HEAD_INITIALIZER(console_list);

static void sinks_lock()
{
	spinlock_lock_noipl(&sink_list_lock);
}

static void sinks_unlock()
{
	spinlock_unlock_noipl(&sink_list_lock);
}

void console_register(struct console *console)
{
	int ipl = spinlock_lock(&console_list_lock);
	TAILQ_INSERT_TAIL(&console_list, console, console_list_entry);
	spinlock_unlock(&console_list_lock, ipl);
}

void sink_add(struct console *console)
{
	int state = spinlock_lock_interrupts(&sink_list_lock);
	TAILQ_INSERT_TAIL(&sink_list, console, sink_list_entry);
	spinlock_unlock_interrupts(&sink_list_lock, state);
}

void sink_remove(struct console *console)
{
	int state = spinlock_lock_interrupts(&sink_list_lock);
	TAILQ_REMOVE(&sink_list, console, sink_list_entry);
	spinlock_unlock_interrupts(&sink_list_lock, state);
}

void sink_foreach(void (*cb)(struct console *, void *), void *private)
{
	struct console *console;

	if (unlikely(is_in_panic())) {
		TAILQ_FOREACH(console, &console_list, console_list_entry)
		{
			cb(console, private);
		}
	} else {
		sinks_lock();
		TAILQ_FOREACH(console, &sink_list, sink_list_entry)
		{
			cb(console, private);
		}
		sinks_unlock();
	}
}
