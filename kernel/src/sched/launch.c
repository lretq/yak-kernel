#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <yak/vm/map.h>
#include <yak/fs/vfs.h>
#include <yak/sched.h>
#include <yak/elf_common.h>
#include <yak/log.h>
#include <yak/status.h>
#include <yak/elf.h>
#include <yak/file.h>
#include <yak/macro.h>
#include <yak/heap.h>

struct auxv_pair {
	uint64_t type;
	uint64_t value;
};
_Static_assert(sizeof(struct auxv_pair) == 16);

static size_t setup_auxv(struct auxv_pair *auxv, struct load_info *info)
{
	size_t i = 0;
	auxv[i++] = (struct auxv_pair){ AT_PHDR, info->phdr };
	auxv[i++] = (struct auxv_pair){ AT_PHENT, info->phent };
	auxv[i++] = (struct auxv_pair){ AT_PHNUM, info->phnum };
	auxv[i++] = (struct auxv_pair){ AT_ENTRY, info->prog_entry };
	auxv[i++] = (struct auxv_pair){ AT_PAGESZ, PAGE_SIZE };
	auxv[i++] =
		(struct auxv_pair){ AT_BASE,
				    info->base }; // ld.so base if PIE/interp
	auxv[i++] = (struct auxv_pair){ AT_UID, 0 };
	auxv[i++] = (struct auxv_pair){ AT_EUID, 0 };
	auxv[i++] = (struct auxv_pair){ AT_GID, 0 };
	auxv[i++] = (struct auxv_pair){ AT_EGID, 0 };
	return i;
}

status_t launch_elf(struct kprocess *proc, char *path, int priority,
		    char **argv_strings, char **envp_strings,
		    struct kthread **thread_out)
{
	assert(proc);
	assert(path);
	assert(argv_strings);
	assert(envp_strings);

	struct vnode *vn;
	status_t status = vfs_open(path, NULL, 0, &vn);
	IF_ERR(status)
	{
		return status;
	}

	struct kthread *thrd = kmalloc(sizeof(struct kthread));
	assert(thrd);
	kthread_init(thrd, argv_strings[0], priority, proc, 1);

	// Allocate kernel stack
	vaddr_t stack_addr = (vaddr_t)vm_kalloc(KSTACK_SIZE, 0);
	stack_addr += KSTACK_SIZE;

	thrd->kstack_top = (void *)stack_addr;

	// make sure we're in the right vm context
	vm_map_tmp_switch(proc->map);
	struct load_info info;
	// load the elf into our address space
	EXPECT(elf_load(vn, proc, &info, 0));

	// allocate stack afterwards, as proc may be static and not relocatable
	vaddr_t user_stack_addr;
	EXPECT(vm_map(proc->map, NULL, USER_STACK_LENGTH, 0, VM_RW | VM_USER,
		      VM_INHERIT_COPY, VM_CACHE_DEFAULT, USER_STACK_BASE, 0,
		      &user_stack_addr));
	user_stack_addr += USER_STACK_LENGTH;
	assert((user_stack_addr & 15) == 0);

	size_t argc = 0;
	for (size_t i = 0; argv_strings[i]; i++) {
		argc++;
	}

	size_t envc = 0;
	for (size_t i = 0; envp_strings[i]; i++) {
		envc++;
	}

	char **argv_ptr = kzalloc(argc * sizeof(char *));
	guard(autofree)(argv_ptr, argc * sizeof(char *));

	char **envp_ptr = kzalloc(envc * sizeof(char *));
	guard(autofree)(envp_ptr, envc * sizeof(char *));

	for (size_t i = 0; i < argc; i++) {
		size_t len = strlen(argv_strings[i]) + 1;
		user_stack_addr -= len;
		memcpy((void *)user_stack_addr, argv_strings[i], len);
		argv_ptr[i] = (void *)user_stack_addr;
	}

	for (size_t i = 0; i < envc; i++) {
		size_t len = strlen(envp_strings[i]) + 1;
		user_stack_addr -= len;
		memcpy((void *)user_stack_addr, envp_strings[i], len);
		envp_ptr[i] = (void *)user_stack_addr;
	}

	user_stack_addr = ALIGN_DOWN(user_stack_addr, 16);

	struct auxv_pair auxv[16];
	size_t auxvc = setup_auxv(auxv, &info);

	// plus envc words + null word
	// plus argc words + null word
	// plus one argc word for count
	// plus one null auxv
	// = counts + 4
	size_t words = envc + argc + auxvc * 2 + 4;

	// make sure we end up with an aligned stack
	// sysv mandates 16 byte aligned stack
	if (!IS_ALIGNED_POW2(user_stack_addr - words * sizeof(uintptr_t), 16)) {
		user_stack_addr -= 8;
	}

	uintptr_t *sp = (uintptr_t *)user_stack_addr;

	*--sp = AT_NULL;
	for (size_t i = 0; i < auxvc; i++) {
		*--sp = auxv[i].value;
		*--sp = auxv[i].type;
	}

	pr_extra_debug("auxv at %p\n", sp);

	// envp
	*--sp = 0;
	for (size_t i = 0; i < envc; i++) {
		*--sp = (uintptr_t)envp_ptr[envc - i - 1];
	}

	pr_extra_debug("envp at %p\n", sp);

	// argv
	*--sp = 0;
	for (size_t i = 0; i < argc; i++) {
		*--sp = (uintptr_t)argv_ptr[argc - i - 1];
	}

	pr_extra_debug("argv at %p\n", sp);

	*--sp = argc;

	pr_extra_debug("argc at %p\n", sp);

	vm_map_tmp_disable();

	assert(IS_ALIGNED_POW2((uintptr_t)sp, 16));
	kthread_context_init(thrd, thrd->kstack_top, kernel_enter_userspace,
			     (void *)info.real_entry, sp);

	*thread_out = thrd;

	return YAK_SUCCESS;
}
