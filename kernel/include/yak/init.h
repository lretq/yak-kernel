#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __cplusplus
#define INIT_MODIFIER_START extern "C" {
#define INIT_MODIFIER_END }
#define INIT_DECL_MODIFIER "C"
#else
#define INIT_MODIFIER_START
#define INIT_MODIFIER_END
#define INIT_DECL_MODIFIER
#endif

#include <stddef.h>
#include <yak/macro.h>
#include <yak/hint.h>

typedef struct init_node init_node_t;
typedef struct init_stage init_stage_t;
typedef void (*init_func_t)(void);

typedef struct init_node {
	const char *name;
	init_func_t func;

	init_stage_t **entails_stages;
	size_t entails_count;

	// NULL terminated array
	init_node_t **deps;

	bool executed;

	init_node_t *next;
} init_node_t;

#define INIT_MAX_STAGE_DEPS 64
struct init_stage {
	const char *name;
	init_node_t *phony_node;
	init_node_t *phony_deps[INIT_MAX_STAGE_DEPS + 1];
};

// Found on SO:
// https://stackoverflow.com/a/11920694
//
// had to slightly change comma expansion; commas have been removed form APPLY_*

/* This counts the number of args */
#define NARGS_SEQ(_0, _1, _2, _3, _4, _5, _6, _7, _8, N, ...) N
#define NARGS(...) NARGS_SEQ(0, ##__VA_ARGS__, 8, 7, 6, 5, 4, 3, 2, 1, 0)

/* This will let macros expand before concating them */
#define PRIMITIVE_CAT(x, y) x##y
#define CAT(x, y) PRIMITIVE_CAT(x, y)

/* This will call a macro on each argument passed in */
#define APPLY(macro, ...) CAT(APPLY_, NARGS(__VA_ARGS__))(macro, __VA_ARGS__)
#define APPLY_0(m, ...)

#define APPLY_0(m, ...)
#define APPLY_1(m, x1) m(x1)
#define APPLY_2(m, x1, x2) m(x1) m(x2)
#define APPLY_3(m, x1, x2, x3) m(x1) m(x2) m(x3)
#define APPLY_4(m, x1, x2, x3, x4) m(x1) m(x2) m(x3) m(x4)
#define APPLY_5(m, x1, x2, x3, x4, x5) m(x1) m(x2) m(x3) m(x4) m(x5)
#define APPLY_6(m, x1, x2, x3, x4, x5, x6) m(x1) m(x2) m(x3) m(x4) m(x5) m(x6)
#define APPLY_7(m, x1, x2, x3, x4, x5, x6, x7) \
	m(x1) m(x2) m(x3) m(x4) m(x5) m(x6) m(x7)
#define APPLY_8(m, x1, x2, x3, x4, x5, x6, x7, x8) \
	m(x1) m(x2) m(x3) m(x4) m(x5) m(x6) m(x7) m(x8)

#define DECL_EXTERN(x) extern INIT_DECL_MODIFIER init_node_t x;
#define DEP_PTR(x) &x,

#define STAGE_DEP_PTR(x) &x##_struct_stage,

#define INIT_GET_STAGE(stage_name) \
	extern INIT_DECL_MODIFIER init_stage_t stage_name##_struct_stage;

#define INIT_GET_NODE(node_name) \
	extern INIT_DECL_MODIFIER init_node_t node_name;

#define INIT_ENTAILS(node_name, ...)                                        \
	APPLY(INIT_GET_STAGE, ##__VA_ARGS__)                                \
	static init_stage_t *node_name##_entails[] = { APPLY(STAGE_DEP_PTR, \
							     ##__VA_ARGS__) };

#define INIT_DEPS(node_name, ...)                         \
	APPLY(DECL_EXTERN, ##__VA_ARGS__)                 \
	static init_node_t *node_name##_deps[] = { APPLY( \
		DEP_PTR, ##__VA_ARGS__) NULL };

#define INIT_NODE(node_name, node_func)                                           \
	INIT_MODIFIER_START                                                       \
	__attribute__((                                                  \
		section(".init_node"), used, aligned(__alignof__(init_node_t)))) init_node_t node_name = { \
		.name = #node_name,                                      \
		.func = node_func,                                       \
		.entails_stages = node_name##_entails,                   \
		.entails_count = elementsof(node_name##_entails),        \
		.deps = node_name##_deps,                                \
		.executed = false,                                       \
		.next = NULL                                             \
	};                                                               \
	INIT_MODIFIER_END

#define INIT_STAGE(stage_name)                                             \
	INIT_MODIFIER_START                                                \
	init_node_t stage_name##_stage = { .name = #stage_name "_stage",   \
					   .func = NULL,                   \
					   .entails_stages = NULL,         \
					   .entails_count = 0,             \
					   .deps = NULL,                   \
					   .executed = false,              \
					   .next = NULL };                 \
	__attribute__((section(".init_stage"), used, aligned(__alignof__(init_stage_t)))) init_stage_t        \
		stage_name##_struct_stage = { .name = #stage_name,         \
					      .phony_node =                \
						      &stage_name##_stage, \
					      .phony_deps = { NULL } };    \
	INIT_MODIFIER_END

#define INIT_RUN_STAGE(stage_name)                          \
	do {                                                \
		INIT_GET_STAGE(stage_name);                 \
		init_stage_run(&stage_name##_struct_stage); \
	} while (0)

#define INIT_RUN_NODE(node_name)           \
	do {                               \
		INIT_GET_NODE(node_name);  \
		init_node_run(&node_name); \
	}

void init_setup();

void init_node_register(init_node_t *node);
void init_stage_register(init_stage_t *stage);

void init_node_run(init_node_t *node);
void init_stage_run(init_stage_t *stage);

void init_run_all();

#ifdef __cplusplus
}
#endif
