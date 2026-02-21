#pragma once

#include <concepts>
#include <yak/sched.h>
#include <yak/mutex.h>

namespace yak
{

template <typename T>
concept Lockable = requires(T l) {
	{ l.lock() } -> std::same_as<status_t>;
	{ l.unlock() } -> std::same_as<void>;
};

struct adopt_lock_t {
	explicit adopt_lock_t() = default;
};

inline constexpr adopt_lock_t adopt_lock{};

template <Lockable LockType> class LockGuard {
    public:
	explicit LockGuard(LockType &lock)
		: lockRef_(lock)
		, locked_(true)
	{
		lockRef_.lock();
	}

	LockGuard(LockType &lock, yak::adopt_lock_t)
		: lockRef_(lock)
		, locked_(true)
	{
		// Do NOT call lock
	}

	LockGuard(const LockGuard &) = delete;
	LockGuard &operator=(const LockGuard &) = delete;
	LockGuard(LockGuard &&other) = delete;
	LockGuard &operator=(const LockGuard &&) = delete;

	~LockGuard()
	{
		unlock();
	}

	void unlock()
	{
		if (locked_) {
			locked_ = false;
			lockRef_.unlock();
		}
	}

	void lock()
	{
		if (!locked_) {
			lockRef_.lock();
			locked_ = true;
		}
	}

    private:
	LockType &lockRef_;
	bool locked_;
};

}
