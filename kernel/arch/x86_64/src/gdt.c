#include <stdint.h>
#include <yak/vm.h>
#include <yak/vm/map.h>
#include <yak/percpu.h>
#include <yak/cpudata.h>
#include <yak/heap.h>
#include <yak/init.h>
#include <string.h>

#include "gdt.h"
#include "tss.h"

struct [[gnu::packed]] gdt_entry {
	uint16_t limit;
	uint16_t low;
	uint8_t mid;
	uint8_t access;
	uint8_t granularity;
	uint8_t high;
};

struct [[gnu::packed]] gdt_tss_entry {
	uint16_t length;
	uint16_t low;
	uint8_t mid;
	uint8_t access;
	uint8_t flags;
	uint8_t high;
	uint32_t upper;
	uint32_t rsv;
};

struct [[gnu::packed]] gdt {
	struct gdt_entry entries[GDT_TSS];
	struct gdt_tss_entry tss;
};

struct gdt percpu_gdt PERCPU;
struct tss percpu_tss PERCPU;

#define PERCPU_GDT_ENTRY(index) (PERCPU_PTR(percpu_gdt.entries[index]))
#define PERCPU_TSS_ENTRY() (PERCPU_PTR(percpu_gdt.tss))

static void gdt_make_entry(int index, uint32_t base, uint16_t limit,
			   uint8_t access, uint8_t granularity)
{
	struct gdt_entry entry;
	entry.limit = limit;
	entry.access = access;
	entry.granularity = granularity;
	entry.low = (uint16_t)base;
	entry.mid = (uint8_t)(base >> 16);
	entry.high = (uint8_t)(base >> 24);

	struct gdt_entry *e = PERCPU_GDT_ENTRY(index);
	*e = entry;
}

static void gdt_tss_entry()
{
	struct gdt_tss_entry tss_entry = { 0 };
	tss_entry.length = 104;
	tss_entry.access = 0x89;

	struct gdt_tss_entry *e = PERCPU_TSS_ENTRY();
	*e = tss_entry;
}

void gdt_init()
{
	gdt_make_entry(GDT_NULL, 0, 0, 0, 0);
	gdt_make_entry(GDT_KERNEL_CODE, 0, 0, 0x9a, 0x20);
	gdt_make_entry(GDT_KERNEL_DATA, 0, 0, 0x92, 0);
	gdt_make_entry(GDT_USER_DATA, 0, 0, 0xf2, 0);
	gdt_make_entry(GDT_USER_CODE, 0, 0, 0xfa, 0x20);
	gdt_tss_entry();
}

static vaddr_t alloc_kstack()
{
	vaddr_t stack_addr = (vaddr_t)vm_kalloc(KSTACK_SIZE, 0);
	assert(stack_addr != 0);
	return stack_addr + KSTACK_SIZE;
}

static void tss_reload()
{
	// we mustn't access another cpu's tss
	ipl_t ipl = ripl(IPL_DPC);

	uint64_t tss_addr = (uint64_t)PERCPU_PTR(percpu_tss);
	struct gdt_tss_entry *tss_entry = PERCPU_PTR(percpu_gdt.tss);
	tss_entry->low = (uint16_t)tss_addr;
	tss_entry->mid = (uint8_t)(tss_addr >> 16);
	tss_entry->high = (uint8_t)(tss_addr >> 24);
	tss_entry->upper = (uint32_t)(tss_addr >> 32);

	tss_entry->flags = 0;
	tss_entry->access = 0b10001001;
	tss_entry->rsv = 0;

	asm volatile("ltr %0" ::"rm"(GDT_SEL_TSS) : "memory");

	xipl(ipl);
}

void tss_init()
{
	struct tss *tss_ptr = PERCPU_PTR(percpu_tss);
	memset(tss_ptr, 0, sizeof(struct tss));
	tss_ptr->ist1 = alloc_kstack();
	tss_ptr->ist2 = alloc_kstack();
	tss_ptr->ist3 = alloc_kstack();
	tss_ptr->ist4 = alloc_kstack();
	tss_reload();
}
INIT_ENTAILS(x86_bsp_tss, bsp_ready);
INIT_DEPS(x86_bsp_tss, heap_ready_stage);
INIT_NODE(x86_bsp_tss, tss_init);

void gdt_reload()
{
	struct [[gnu::packed]] {
		uint16_t limit;
		uint64_t base;
	} gdtr = {
		.limit = sizeof(struct gdt) - 1,
		.base = (uint64_t)PERCPU_PTR(percpu_gdt),
	};

	asm volatile( //
		"lgdt (%0)\n\t"
		// reload CS
		"pushq %1\n\t"
		"leaq 1f(%%rip), %%rax\n\t"
		"pushq %%rax\n\t"
		"lretq\n\t"
		"1:\n\t"
		// reload DS
		"mov %2, %%ds\n\t"
		"mov %2, %%es\n\t"
		"mov %2, %%ss\n\t"
		"xor %%eax, %%eax\n\t"
		"mov %%ax, %%fs\n\t"
		//
		:
		: "r"(&gdtr), "i"((uint16_t)GDT_SEL_KERNEL_CODE),
		  "r"(GDT_SEL_KERNEL_DATA)
		: "rax", "memory"
		//
	);
}
