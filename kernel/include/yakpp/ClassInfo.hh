#pragma once

namespace yak
{

class Object;

class ClassInfo {
    public:
	const char *className = nullptr;
	const ClassInfo *superClass = nullptr;
	Object *(*createInstance)() = nullptr;
};

}
