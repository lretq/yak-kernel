#pragma once

#include <stddef.h>
#include <yak/hint.h>
#include <yak/cleanup.h>

typedef size_t refcount_t;

#define DECLARE_REFMAINT(type)           \
	void type##_ref(struct type *p); \
	void type##_deref(struct type *p);

#define GENERATE_REFMAINT_INTERNAL(fn_attr, field, type, destructor)           \
	__used __no_san fn_attr void type##_ref(struct type *p)                       \
	{                                                                      \
		__atomic_fetch_add(&p->field, 1, __ATOMIC_ACQUIRE);            \
	}                                                                      \
	__used __no_san fn_attr void type##_deref(struct type *p)                     \
	{                                                                      \
		if (__atomic_fetch_sub(&p->field, 1, __ATOMIC_ACQ_REL) == 1) { \
			destructor(p);                                         \
		}                                                              \
	}

#define GENERATE_REFMAINT(t, f, d) GENERATE_REFMAINT_INTERNAL(, f, t, d)

#define GENERATE_REFMAINT_INLINE(t, f, d) \
	GENERATE_REFMAINT_INTERNAL(static inline, f, t, d)

DEFINE_CLEANUP_CLASS(
	ref,
	{
		void *ptr;
		void (*dtor)(void *);
	},
	{
		if (ctx && ctx->ptr && ctx->dtor)
			ctx->dtor(ctx->ptr);
	},
	{
		if (ctor != NULL) {
			ctor(ptr);
		}
		GUARD_RET(ref, .ptr = ptr, .dtor = dtor);
	},
	void *ptr, void (*ctor)(void *), void (*dtor)(void *));

#define guard_ref_adopt(obj, type) \
	guard(ref)((obj), NULL, (void (*)(void *))type##_deref)
#define guard_ref(obj, type)                            \
	guard(ref)((obj), (void (*)(void *))type##_ref, \
		   (void (*)(void *))type##_deref)
