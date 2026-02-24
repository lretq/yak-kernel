#include <stddef.h>
#include <string.h>
#include <stdint.h>
#include <yak/panic.h>
#include <yak/vm/kmem.h>
#include <yak/init.h>

#include "asm.h"
#include "fpu.h"

static kmem_cache_t *fpstate_cache;
static size_t fpstate_size;
static enum { FP_FXSAVE, FP_XSAVE } fp_ctx_type;

static void local_fpu_enable(bool xsave)
{
	uint64_t cr0 = read_cr0();
	cr0 &= ~(1UL << 2); // disable emulation
	cr0 |= (1 << 1); // monitor co-processor
	write_cr0(cr0);

	uint64_t cr4 = read_cr4();
	cr4 |= (1 << 9); // osfxsr
	cr4 |= (1 << 10); // osxmmexcpt
	write_cr4(cr4);

	if (xsave) {
		// enable xsetbv/xgetbv + xsave/xrstor
		cr4 = read_cr4();
		cr4 |= (1 << 18);
		write_cr4(cr4);

		uint64_t xcr0 = 0;
		xcr0 |= (1 << 0);

		uint32_t eax, ebx, ecx, edx;
		asm_cpuid(0xd, 0, &eax, &ebx, &ecx, &edx);
		// enable everything supported
		xcr0 |= ((uint64_t)edx << 32) | eax;

		xsetbv(0, xcr0);
	}
}

void fpu_ap_init()
{
	local_fpu_enable(fp_ctx_type != FP_FXSAVE);
}

void fpu_init()
{
	uint32_t eax, ebx, ecx, edx;

	asm_cpuid(1, 0, &eax, &ebx, &ecx, &edx);

	if ((edx & (1 << 25)) == 0)
		panic("SSE is unsupported. how did you even boot into long mode?\n");

	// check if xsave is supported
	if (ecx & (1 << 26)) {
		local_fpu_enable(true);

		asm_cpuid(0xd, 0, &eax, &ebx, &ecx, &edx);
		fpstate_cache = kmem_cache_create("fpu_state", ebx, 64, NULL,
						  NULL, NULL, NULL, NULL,
						  KM_SLEEP);

		fpstate_size = ebx;
		fp_ctx_type = FP_XSAVE;
	} else {
		local_fpu_enable(false);

		fpstate_cache = kmem_cache_create("fpu_state", 512, 16, NULL,
						  NULL, NULL, NULL, NULL,
						  KM_SLEEP);
		fpstate_size = 512;
		fp_ctx_type = FP_FXSAVE;
	}
}

INIT_ENTAILS(x86_fpu_setup, bsp_ready);
INIT_DEPS(x86_fpu_setup, heap_ready_stage);
INIT_NODE(x86_fpu_setup, fpu_init);

void *fpu_alloc()
{
	void *ptr = kmem_cache_alloc(fpstate_cache, KM_SLEEP);
	memset(ptr, 0, fpstate_size);
	if (fp_ctx_type == FP_XSAVE) {
		fpu_save(ptr);
	}
	return ptr;
}

size_t fpu_statesize()
{
	return fpstate_size;
}

void fpu_free(void *ptr)
{
	kmem_cache_free(fpstate_cache, ptr);
}

void fpu_save(void *ptr)
{
	switch (fp_ctx_type) {
	case FP_FXSAVE:
		asm volatile("fxsaveq (%0)" ::"r"(ptr));
		break;
	case FP_XSAVE:
		asm volatile("xsaveq (%0)" ::"r"(ptr), "a"(-1), "d"(-1));
		break;
	}
}

void fpu_restore(void *ptr)
{
	switch (fp_ctx_type) {
	case FP_FXSAVE:
		asm volatile("fxrstorq (%0)" ::"r"(ptr));
		break;
	case FP_XSAVE:
		asm volatile("xrstorq (%0)" ::"r"(ptr), "a"(-1), "d"(-1));
		break;
	}
}
