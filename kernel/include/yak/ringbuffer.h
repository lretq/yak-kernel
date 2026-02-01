#pragma once

#include <stddef.h>
#include <yak/status.h>

struct ringbuffer {
	unsigned char *data;
	size_t capacity;
	size_t size;
	size_t head;
	size_t tail;
};

status_t ringbuffer_init(struct ringbuffer *rb, size_t cap);
status_t ringbuffer_static_init(struct ringbuffer *rb, size_t cap,
				void *backing);

void ringbuffer_destroy(struct ringbuffer *rb);

/* neither put nor get is thread safe. users must bring their own sync. */
size_t ringbuffer_put(struct ringbuffer *rb, const void *buf, size_t count);
size_t ringbuffer_get(struct ringbuffer *rb, void *buf, size_t count);
size_t ringbuffer_available(struct ringbuffer *rb);
