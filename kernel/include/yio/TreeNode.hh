#pragma once

#include <assert.h>
#include <cstddef>
#include <yak/queue.h>
#include <yakpp/LockGuard.hh>
#include <yakpp/SpinLock.hh>
#include <yakpp/String.hh>
#include <yakpp/Object.hh>

namespace yak::io
{

struct TreeNode : public Object {
	IO_OBJ_DECLARE(TreeNode);

    public:
	void init() override
	{
		Object::init();

		parent_ = nullptr;
		TAILQ_INIT(&children_);
		lock_.init();
	}

	void deinit() override
	{
		assert(TAILQ_EMPTY(&children_));
		assert(parent_ == nullptr);
	}

	void attachChild(TreeNode *child)
	{
		assert(child);
		assert(!child->parent_);

		LockGuard lguard(lock_);

		TAILQ_INSERT_TAIL(&children_, child, list_entry_);
		__atomic_fetch_add(&childcount, 1, __ATOMIC_ACQUIRE);

		child->parent_ = this;

		child->retain();
		this->retain();
	}

	void attachChildAndUnref(TreeNode *child)
	{
		attachChild(child);
		child->release();
	}

	void detachChild(TreeNode *child)
	{
		assert(child);
		assert(child->parent_ == this);

		LockGuard lguard(lock_);

		TAILQ_REMOVE(&children_, child, list_entry_);
		__atomic_fetch_sub(&childcount, 1, __ATOMIC_RELEASE);

		child->parent_ = nullptr;

		child->release();
		this->release();
	}

	void attachParent(TreeNode *parent)
	{
		parent->attachChild(this);
	}

	TreeNode *getParent() const
	{
		return parent_;
	}

	String *name = nullptr;

	TreeNode *parent_;

	size_t childcount = 0;
	TAILQ_HEAD(, TreeNode) children_;

	TAILQ_ENTRY(TreeNode) list_entry_;

	SpinLock lock_;
};

}
