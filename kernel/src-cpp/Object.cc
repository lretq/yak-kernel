#include <yakpp/Object.hh>

namespace yak
{

// superclass of all classes
IO_OBJ_DEFINE_ROOT(Object);

bool Object::isEqual(Object *other) const
{
	return other->getClassInfo() == getClassInfo();
}

}
