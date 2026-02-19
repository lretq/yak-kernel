#include <assert.h>
#include <string.h>
#include <yak/heap.h>
#include <yak/status.h>
#include <yak/ringbuffer.h>

status_t ringbuffer_init(struct ringbuffer *rb, size_t cap)
{
	if (!rb || !cap)
		return YAK_INVALID_ARGS;

	rb->data = kmalloc(cap);
	if (!rb->data)
		return YAK_OOM;

	rb->capacity = cap;
	rb->head = 0;
	rb->tail = 0;
	rb->size = 0;

	return YAK_SUCCESS;
}

status_t ringbuffer_static_init(struct ringbuffer *rb, size_t cap,
				void *backing)
{
	if (!rb || !cap || !backing)
		return YAK_INVALID_ARGS;

	rb->data = backing;
	rb->capacity = cap;
	rb->head = 0;
	rb->tail = 0;
	rb->size = 0;

	return YAK_SUCCESS;
}

void ringbuffer_destroy(struct ringbuffer *rb)
{
	assert(rb);
	kfree(rb->data, rb->capacity);
}

size_t ringbuffer_put(struct ringbuffer *rb, const void *buf, size_t count)
{
	assert(rb);
	assert(buf);

	if (count == 0)
		return 0;

	size_t free = rb->capacity - rb->size;
	// full
	if (free == 0)
		return 0;

	// we may not have enough space for everything
	size_t write_size = (count < free) ? count : free;

	size_t first_chunk = rb->capacity - rb->head;
	if (first_chunk > write_size)
		first_chunk = write_size;

	memcpy(rb->data + rb->head, buf, first_chunk);

	size_t remaining = write_size - first_chunk;
	if (remaining > 0) {
		memcpy(rb->data, (const unsigned char *)buf + first_chunk,
		       remaining);
	}

	rb->head = (rb->head + write_size) % rb->capacity;
	rb->size += write_size;

	return write_size;
}

size_t ringbuffer_get(struct ringbuffer *rb, void *buf, size_t count)
{
	assert(rb);
	assert(buf);

	if (count == 0)
		return 0;

	size_t avail = rb->size;
	if (avail == 0)
		return 0;

	size_t read_size = (count < avail) ? count : avail;

	size_t first_chunk = rb->capacity - rb->tail;
	if (first_chunk > read_size)
		first_chunk = read_size;

	memcpy(buf, rb->data + rb->tail, first_chunk);

	size_t remaining = read_size - first_chunk;
	if (remaining > 0) {
		memcpy((unsigned char *)buf + first_chunk, rb->data, remaining);
	}

	rb->tail = (rb->tail + read_size) % rb->capacity;
	rb->size -= read_size;

	return read_size;
}

size_t ringbuffer_available(struct ringbuffer *rb)
{
	return rb->size;
}
