#define pr_fmt(fmt) "init: " fmt

#include <assert.h>
#include <stddef.h>
#include <string.h>
#include <nanoprintf.h>
#include <yak/init.h>
#include <yak/kernel-file.h>
#include <yak/log.h>
#include <yak/panic.h>
#include <yak/macro.h>

// After looking at other impls to see how they do init
// I noticed this looks an awful lot like Astrals init
// routine stuff. This is not inspired by it, I just didn't
// want to implement any 'true' graph stuff. Even the macro
// slop is similar. Funney.
//
// https://github.com/Mathewnd/Astral/blob/rewrite/kernel-src/include/kernel/init.h

// possible improvements could be:
// - building a DAG
// - stages (-> xxx.ready)
// - binary search for node names (Actually shouldn't be too hard? Just have the linker sort by symbol name? But it's not really needed as I do it statically now and replaced the searching anyway)

static init_node_t *nodelist = NULL;

void init_node_register(init_node_t *node)
{
	init_node_t *cur = nodelist;

	if (!cur) {
		nodelist = node;
		return;
	}

	while (cur && cur->next)
		cur = cur->next;

	cur->next = node;
}

void init_node_run(init_node_t *node)
{
	assert(node);
	if (node->executed)
		return;

	init_node_t **dep = node->deps;
	while (*dep) {
		init_node_run(*dep);
		dep++;
	}

	// may be a phony/stage node
	if (node->func) {
		pr_info("running task '%s'\n", node->name);
		node->func();
	}
	node->executed = true;
}

void init_run_all()
{
	init_node_t *node = nodelist;
	while (node) {
		if (!node->executed)
			init_node_run(node);
		node = node->next;
	}
}

void init_stage_register(init_stage_t *stage)
{
	size_t stagedep_count = 0;

	init_node_t *cur = nodelist;
	while (cur && cur->next) {
		for (size_t i = 0; i < cur->entails_count; i++) {
			if (stage == cur->entails_stages[i]) {
				if (i + 1 > INIT_MAX_STAGE_DEPS) {
					panic("try to entail more than " STR(
						INIT_MAX_STAGE_DEPS) " nodes on a single stage!");
				}
				stage->phony_deps[stagedep_count++] = cur;
			}
		}
		cur = cur->next;
	}

	stage->phony_node->deps = stage->phony_deps;
	init_node_register(stage->phony_node);

	pr_warn("stage (%s) dep count: %ld\n", stage->name, stagedep_count);
}

void init_stage_run(init_stage_t *stage)
{
	if (stage->phony_node->executed)
		return;

	init_node_run(stage->phony_node);
	pr_info("reached stage '%s'\n", stage->name);
}

extern init_node_t __kernel_init_node_start[];
extern init_node_t __kernel_init_node_end[];

extern char __kernel_init_stage_start[];
extern char __kernel_init_stage_end[];

void init_setup()
{
	size_t task_count = 0;

	for (init_node_t *node = __kernel_init_node_start;
	     node < __kernel_init_node_end; node++) {
		init_node_register(node);
		task_count++;
	}

	size_t stage_count = 0;
	init_stage_t *start_stage = (init_stage_t *)__kernel_init_stage_start;
	init_stage_t *end_stage = (init_stage_t *)__kernel_init_stage_end;

	for (init_stage_t *stage = start_stage; stage < end_stage; stage++) {
		init_stage_register(stage);
		stage_count++;
	}

	pr_info("registered %ld tasks and %ld stages\n", task_count,
		stage_count);
}
