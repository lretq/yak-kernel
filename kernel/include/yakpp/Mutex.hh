#pragma once

#include <yak/types.h>
#include <yak/wait.h>
#include <yak/mutex.h>

namespace yak
{

// C++ wrapper around kmutex
class Mutex {
    public:
	Mutex()
	{
	}

	~Mutex()
	{
	}

	Mutex(const Mutex &) = delete;
	Mutex &operator=(const Mutex &) = delete;
	Mutex(Mutex &&other) noexcept = delete;
	Mutex &operator=(Mutex &&other) noexcept = delete;

	void init(const char *name = nullptr)
	{
		kmutex_init(&mutex_, name);
	}

	// May only fail when timeout is given
	status_t lock(nstime_t timeout = TIMEOUT_INFINITE)
	{
		return kmutex_acquire(&mutex_, timeout);
	}

	void unlock()
	{
		kmutex_release(&mutex_);
	}

	struct kmutex *get()
	{
		return &mutex_;
	}

    private:
	struct kmutex mutex_;
};

}
