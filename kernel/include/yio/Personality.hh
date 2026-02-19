#pragma once

#include <yakpp/Object.hh>
#include <yak/queue.h>

namespace yak::io
{

struct Personality : public Object {
	IO_OBJ_DECLARE(Personality);

	friend class IoRegistry;

    public:
	virtual const ClassInfo *getDeviceClass() const = 0;
	virtual bool isEqual(Object *other) const override = 0;

    private:
	TAILQ_ENTRY(Personality) list_entry;
};

}
