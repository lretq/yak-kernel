#pragma once

#include <yakpp/ClassInfo.hh>

#define IO_OBJ_DECLARE_COMMON(className) \
    public:                              \
	static yak::ClassInfo classInfo; \
	constexpr className()            \
	{                                \
	}                                \
	virtual ~className()             \
	{                                \
	}

#define IO_OBJ_DECLARE_ROOT(className)   \
	IO_OBJ_DECLARE_COMMON(className) \
	virtual const yak::ClassInfo *getClassInfo() const;

#define IO_OBJ_DECLARE(className)                                    \
    public:                                                          \
	IO_OBJ_DECLARE_COMMON(className)                             \
	virtual const yak::ClassInfo *getClassInfo() const override; \
	static yak::Object *createInstance();

#define IO_OBJ_DEFINE_COMMON(_className, superClassInfo, instance) \
	const yak::ClassInfo *_className::getClassInfo() const     \
	{                                                          \
		return &_className::classInfo;                     \
	}                                                          \
	yak::ClassInfo _className::classInfo = {                   \
		.className = #_className,                          \
		.superClass = superClassInfo,                      \
		.createInstance = instance,                        \
	};

#define IO_OBJ_DEFINE_VIRTUAL(_className, superClass) \
	IO_OBJ_DEFINE_COMMON(_className, &superClass::classInfo, nullptr)

#define IO_OBJ_DEFINE(_className, superClass)                    \
	IO_OBJ_DEFINE_COMMON(_className, &superClass::classInfo, \
			     _className::createInstance)         \
	yak::Object *_className::createInstance()                \
	{                                                        \
		return new _className();                         \
	}

#define IO_OBJ_DEFINE_ROOT(className) \
	IO_OBJ_DEFINE_COMMON(className, nullptr, nullptr)

#define IO_OBJ_CREATE(className) ((className *)className::createInstance())

#define ALLOC_INIT(var, className)              \
	do {                                    \
		var = IO_OBJ_CREATE(className); \
		(var)->init();                  \
	} while (0)
