#pragma once

#include <yak/spinlock.h>
#include <yak/status.h>

namespace yak
{

class SpinLock {
    public:
	SpinLock()
	{
	}

	~SpinLock()
	{
	}

	SpinLock(const SpinLock &) = delete;
	SpinLock &operator=(const SpinLock &) = delete;
	SpinLock(SpinLock &&other) noexcept = delete;
	SpinLock &operator=(SpinLock &&other) noexcept = delete;

	void init()
	{
		spinlock_init(&mutex_);
	}

	// May only fail when timeout is given
	status_t lock()
	{
		old_ipl_ = spinlock_lock(&mutex_);
		return YAK_SUCCESS;
	}

	void unlock()
	{
		spinlock_unlock(&mutex_, old_ipl_);
	}

	struct spinlock *get()
	{
		return &mutex_;
	}

    private:
	ipl_t old_ipl_;
	struct spinlock mutex_;
};

}
