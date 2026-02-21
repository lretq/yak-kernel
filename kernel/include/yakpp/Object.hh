#pragma once

#include <cstddef>
#include <string.h>
#include <yakpp/meta.hh>

namespace yak
{

class Object {
    public:
	IO_OBJ_DECLARE_ROOT(Object);

	bool isKindOf(const char *name) const
	{
		const ClassInfo *info = getClassInfo();
		while (info) {
			if (strcmp(info->className, name) == 0)
				return true;
			info = info->superClass;
		}
		return false;
	}

	bool isKindOf(const ClassInfo *classInfo) const
	{
		return isKindOf(classInfo->className);
	}

	void retain()
	{
		__atomic_fetch_add(&refcnt_, 1, __ATOMIC_ACQUIRE);
	}

	void release()
	{
		if (__atomic_sub_fetch(&refcnt_, 1, __ATOMIC_RELEASE) == 0) {
			// commit suicide
			deinit();
			delete this;
		}
	}

	virtual void init()
	{
		refcnt_ = 1;
	}

	virtual void deinit()
	{
	}

	virtual bool isEqual(Object *other) const;

	template <typename T> constexpr T *cast()
	{
		return static_cast<T *>(this);
	}

	template <typename T> T *safe_cast()
	{
		return (this->isKindOf(&T::classInfo)) ?
			       static_cast<T *>(this) :
			       nullptr;
	}

    private:
	size_t refcnt_;
};

}
