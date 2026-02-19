#pragma once

#include <yak/sched.h>
#include <yak/mutex.h>

namespace yak
{

template <typename LockType> class LockGuard {
    public:
	explicit LockGuard(LockType &lock)
		: lockRef_(lock)
		, locked_(true)
	{
		lockRef_.lock();
	}

	LockGuard(const LockGuard &) = delete;
	LockGuard &operator=(const LockGuard &) = delete;
	LockGuard(LockGuard &&other) = delete;
	LockGuard &operator=(const LockGuard &&) = delete;

	~LockGuard()
	{
		if (locked_)
			lockRef_.unlock();
	}

	void unlock()
	{
		if (locked_)
			lockRef_.unlock();
	}

	void lock()
	{
		if (!locked_)
			lockRef_.lock();
	}

    private:
	LockType &lockRef_;
	bool locked_;
};

}
