#include <stdint.h>
#include <stddef.h>
#include <yak/init.h>
#include <yak/arch-context.h>
#include <yak/arch-cpu.h>
#include <yak/cpudata.h>
#include <yak/cpu.h>
#include <yak/log.h>
#include <yak/io/console.h>
#include <yak/macro.h>
#include <yak/kernel-file.h>
#include <yak/irq.h>
#include <yak/vm/map.h>
#include <yak/heap.h>
#include <yak/vm/pmm.h>
#include <yak/syscall.h>
#include <uacpi/uacpi.h>

#include "asm.h"
#include "gdt.h"
#include "fpu.h"

#define COM1 0x3F8

#define SERIAL_BUFSIZE 64
char serial_buf[SERIAL_BUFSIZE];

size_t serial_pos = 0;

static inline void serial_putc(uint8_t c)
{
	if (c == '\n')
		serial_putc('\r');

	while (!(inb(COM1 + 5) & 0x20))
		asm volatile("pause");

	if ((c >= 0x20 && c <= 0x7E) || c == '\n' || c == '\r' || c == '\t' ||
	    c == '\e' || c == '\b') {
		outb(COM1, c);
	} else {
		outb(COM1, '?');
	}
}

static inline void serial_flush()
{
	for (size_t i = 0; i < serial_pos; i++) {
		serial_putc((uint8_t)serial_buf[i]);
	}
	serial_pos = 0;
}

size_t serial_write([[maybe_unused]] struct console *console, const char *str,
		    size_t len)
{
	int state = disable_interrupts();
	for (size_t i = 0; i < len; i++) {
		serial_buf[serial_pos++] = str[i];

		if (str[i] == '\n' || serial_pos >= SERIAL_BUFSIZE) {
			serial_flush();
		}
	}
	if (state)
		enable_interrupts();

	return len;
}

struct console com1_console = {
	.name = "COM1",
	.write = serial_write,
};

void idt_init();
void idt_reload();
void gdt_init();
void gdt_reload();
void tss_init();

[[gnu::section(".percpu.cpudata"), gnu::used]]
struct cpu percpu_cpudata = {};

extern char __init_stack_top[];

__no_san void plat_syscall_handler(struct syscall_frame *frame)
{
	if (frame->rax >= MAX_SYSCALLS) {
		pr_error("request syscall >= MAX_SYSCALLS\n");
		return;
	}

	struct syscall_result res = syscall_table[frame->rax](
		frame, frame->rdi, frame->rsi, frame->rdx, frame->r10,
		frame->r8, frame->r9);
	frame->rax = res.retval;
	frame->rdx = res.err;
}

// XXX: move this to a seperate .S file!
// needs automatic struct offseting though. find out how to expose them.
[[gnu::naked]]
void _syscall_entry()
{
	asm volatile(
		//
		"cli\n\t"
		// switch to kernel gsbase unconditionally
		"swapgs\n\t"

		// store user rsp in cpu->syscall_temp
		"movq %%rsp, %%gs:__kernel_percpu_start+%c0(%%rip)\n\t"
		// load kernel rsp from cpu->kstack_top
		"movq %%gs:__kernel_percpu_start+%c1(%%rip), %%rsp\n\t"

		// push syscall_temp to stack
		"pushq %%gs:__kernel_percpu_start+%c0(%%rip)\n\t"
		"push %%rbp\n\t"
		"push %%r15\n\t"
		"push %%r14\n\t"
		"push %%r13\n\t"
		"push %%r12\n\t"
		"push %%r11\n\t"
		"push %%r10\n\t"
		"push %%r9\n\t"
		"push %%r8\n\t"
		"push %%rsi\n\t"
		"push %%rdi\n\t"
		"push %%rdx\n\t"
		"push %%rcx\n\t"
		"push %%rbx\n\t"
		"push %%rax\n\t"

		// finally enable interrupts again
		"sti\n\t"
		"xor %%rbp, %%rbp\n\t"
		"mov %%rsp, %%rdi\n\t"
		"call plat_syscall_handler\n\t"

		"pop %%rax\n\t"
		"pop %%rbx\n\t"
		"pop %%rcx\n\t"
		"pop %%rdx\n\t"
		"pop %%rdi\n\t"
		"pop %%rsi\n\t"
		"pop %%r8\n\t"
		"pop %%r9\n\t"
		"pop %%r10\n\t"
		"pop %%r11\n\t"
		"pop %%r12\n\t"
		"pop %%r13\n\t"
		"pop %%r14\n\t"
		"pop %%r15\n\t"
		"pop %%rbp\n\t"

		// cli now, we're switching to user rsp & gsbase again
		"cli\n\t"
		"pop %%rsp\n\t"
		"swapgs\n\t"

		"sysretq\n\t"
		:
		: "i"(offsetof(struct cpu, syscall_temp)),
		  "i"(offsetof(struct cpu, kstack_top))
		:);
}

static void setup_syscall_msrs()
{
	uint64_t efer = rdmsr(MSR_EFER);
	efer |= 1; // set the SCE bit to enable syscall/sysret
	wrmsr(MSR_EFER, efer);
	wrmsr(MSR_LSTAR, (uintptr_t)_syscall_entry);
	uint64_t star = rdmsr(MSR_STAR);
	star |= ((uint64_t)GDT_SEL_USER_SYSCALL << 48);
	star |= ((uint64_t)GDT_SEL_KERNEL_CODE << 32);
	wrmsr(MSR_STAR, star);
}

static void setup_cpu()
{
	setup_syscall_msrs();

	// load the global idt
	idt_reload();

	// initialize percpu gdt
	gdt_init();
	// load our gdt
	gdt_reload();
}

void init_early_output()
{
	// XXX: We may want to check for port e9?
	console_register(&com1_console);
	sink_add(&com1_console);
}

void init_bsp_cpudata()
{
	// we can just use the "normal" variables on the BSP for percpu access
	wrmsr(MSR_GSBASE, 0);

	// can pass the start of the .percpu.cpudata offset for the later inits
	cpudata_init(&percpu_cpudata, (void *)__init_stack_top);

	// initialize the global idt instance
	idt_init();

	setup_cpu();

	init_early_output();
}

void apic_global_init();
void lapic_enable();

void timer_setup()
{
	extern status_t hpet_setup();
	EXPECT(hpet_setup());

	apic_global_init();
	lapic_enable();
}
INIT_ENTAILS(x86_timer_setup, bsp_ready);
INIT_DEPS(x86_timer_setup, early_io_stage);
INIT_NODE(x86_timer_setup, timer_setup);

static struct irq_object ipi_obj;
size_t ipi_vector;

void ipi_handler();

static int arch_ipi_handler([[maybe_unused]] void *context)
{
	ipi_handler();
	return IRQ_ACK;
}

void ipi_setup()
{
	irq_object_init(&ipi_obj, arch_ipi_handler, NULL);
	irq_alloc_ipl(&ipi_obj, IPL_HIGH, 0, PIN_CONFIG_ANY);
	ipi_vector = VEC_TO_IRQ(ipi_obj.slot->vector);
}
INIT_ENTAILS(x86_ipi_setup, bsp_ready);
INIT_DEPS(x86_ipi_setup, x86_timer_setup);
INIT_NODE(x86_ipi_setup, ipi_setup);

#include <limine.h>

#define LIMINE_REQ [[gnu::used, gnu::section(".limine_requests")]]
LIMINE_REQ struct limine_mp_request mp_request = {
	.id = LIMINE_MP_REQUEST,
};

// TODO: Do the SMP startup ourselves

struct extra_info {
	uintptr_t cr3;
	void *stack_top;
	int done;
	uintptr_t percpu_offset;
};

// called from naked function after switching off of the limine stack
[[gnu::used]]
static void c_ap_entry(struct limine_mp_info *info)
{
	disable_interrupts();

	struct extra_info *extra = (struct extra_info *)info->extra_argument;

	wrmsr(MSR_GSBASE, extra->percpu_offset);

	vm_map_activate(kmap());

	struct cpu *cpudata = (struct cpu *)((uintptr_t)&percpu_cpudata +
					     extra->percpu_offset);

	cpudata_init(cpudata, (void *)extra->stack_top);

	setup_cpu();
	fpu_ap_init();
	tss_init();

	lapic_enable();

	__all_cpus[curcpu().cpu_id] = curcpu_ptr();

	__atomic_store_n(&extra->done, 1, __ATOMIC_RELEASE);

	cpu_up(curcpu().cpu_id);

	extern void idle_loop();
	idle_loop();
}

[[gnu::naked]]
static void naked_ap_entry(struct limine_mp_info *info)
{
	asm volatile("\n\txor %%rbp, %%rbp"
		     "\n\tmov %c0(%%rdi), %%rax"
		     "\n\tmov %c1(%%rax), %%rbx"
		     "\n\tmov %%rbx, %%cr3"
		     "\n\tmov %c2(%%rax), %%rsp"
		     "\n\tcall c_ap_entry"
		     //
		     ::"i"(offsetof(struct limine_mp_info, extra_argument)),
		     "i"(offsetof(struct extra_info, cr3)),
		     "i"(offsetof(struct extra_info, stack_top))
		     : "memory");
}

static struct extra_info extra;

void start_aps()
{
	disable_interrupts();

	struct limine_mp_response *response = mp_request.response;

	__all_cpus = kcalloc(response->cpu_count, sizeof(struct cpu *));

	for (size_t i = 0; i < response->cpu_count; i++) {
		struct limine_mp_info *info = response->cpus[i];

		if (info->lapic_id == curcpu().md.apic_id)
			continue;

		extra.cr3 = read_cr3();
		extra.done = 0;
		extra.stack_top =
			(void *)((vaddr_t)vm_kalloc(KSTACK_SIZE, VM_SLEEP) +
				 KSTACK_SIZE);

		size_t percpu_size = (uintptr_t)__kernel_percpu_end -
				     (uintptr_t)__kernel_percpu_start;

		vaddr_t percpu_area = (vaddr_t)vm_kalloc(
			ALIGN_UP(percpu_size, PAGE_SIZE), VM_SLEEP);

		pr_debug("percpu_area: %lx\n", percpu_area);

		extra.percpu_offset =
			percpu_area - (uintptr_t)__kernel_percpu_start;

		info->extra_argument = (uint64_t)&extra;
		info->goto_address = naked_ap_entry;

		while (__atomic_load_n(&extra.done, __ATOMIC_RELAXED) != 1) {
			asm volatile("pause" ::: "memory");
		}
	}

	__all_cpus[curcpu().cpu_id] = curcpu_ptr();

	enable_interrupts();
}

INIT_ENTAILS(x86_smp, aps_ready);
INIT_DEPS(x86_smp, bsp_ready_stage);
INIT_NODE(x86_smp, start_aps);
