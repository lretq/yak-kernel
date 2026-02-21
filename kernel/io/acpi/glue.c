#define pr_fmt(fmt) "uacpi: " fmt

#include <stddef.h>
#include <uacpi/status.h>
#include <uacpi/types.h>
#include <uacpi/kernel_api.h>
#include <yak/log.h>
#include <yak/irq.h>
#include <yak/mutex.h>
#include <yak/cpudata.h>
#include <yak/status.h>
#include <yak/vm/map.h>
#include <yak/vmflags.h>
#include <yak/heap.h>
#include <yak/timer.h>
#include <yak/macro.h>
#include <yak/arch-mm.h>
#include <yak/types.h>
#include <yak/sched.h>
#include <yak/semaphore.h>
#include <yio/pci.h>
#include "uacpi/uacpi.h"
#include "yak/vm/pmm.h"
#include <yak/init.h>

extern paddr_t plat_get_rsdp();

uacpi_status uacpi_kernel_get_rsdp(uacpi_phys_addr *out_rsdp_address)
{
	paddr_t addr = plat_get_rsdp();
	if (addr == 0) {
		*out_rsdp_address = 0;
		return UACPI_STATUS_NOT_FOUND;
	}
	*out_rsdp_address = addr;
	return UACPI_STATUS_OK;
}

void *uacpi_kernel_map(uacpi_phys_addr addr, uacpi_size len)
{
	vaddr_t mapped_addr;
	status_t status = vm_map_mmio(kmap(), addr, len, VM_RW,
				      VM_CACHE_DEFAULT, &mapped_addr);
	if (IS_ERR(status)) {
		pr_error("error while mapping 0x%lx: %s\n", addr,
			 status_str(status));
		return NULL;
	}

	return (void *)mapped_addr;
}

void uacpi_kernel_unmap(void *addr, uacpi_size len)
{
	EXPECT(vm_unmap(kmap(), (vaddr_t)addr, len, 0));
}

void uacpi_kernel_log(uacpi_log_level loglevel, const uacpi_char *logmsg)
{
	size_t translated_level = 6 - loglevel;
	printk(translated_level, "uacpi: %s", logmsg);
}

#ifdef x86_64
uacpi_status uacpi_kernel_io_map(uacpi_io_addr base,
				 [[maybe_unused]] uacpi_size len,
				 uacpi_handle *out_handle)
{
	*out_handle = (uacpi_handle)base;
	return UACPI_STATUS_OK;
}

void uacpi_kernel_io_unmap([[maybe_unused]] uacpi_handle handle)
{
}

#include "../arch/x86_64/src/asm.h"

uacpi_status uacpi_kernel_io_read8(uacpi_handle handle, uacpi_size offset,
				   uacpi_u8 *out_value)
{
	uacpi_io_addr addr = (uacpi_io_addr)handle + offset;
	*out_value = inb(addr);
	return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_io_read16(uacpi_handle handle, uacpi_size offset,
				    uacpi_u16 *out_value)
{
	uacpi_io_addr addr = (uacpi_io_addr)handle + offset;
	*out_value = inw(addr);
	return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_io_read32(uacpi_handle handle, uacpi_size offset,
				    uacpi_u32 *out_value)
{
	uacpi_io_addr addr = (uacpi_io_addr)handle + offset;
	*out_value = inl(addr);
	return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_io_write8(uacpi_handle handle, uacpi_size offset,
				    uacpi_u8 in_value)
{
	uacpi_io_addr addr = (uacpi_io_addr)handle + offset;
	outb(addr, in_value);
	return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_io_write16(uacpi_handle handle, uacpi_size offset,
				     uacpi_u16 in_value)
{
	uacpi_io_addr addr = (uacpi_io_addr)handle + offset;
	outw(addr, in_value);
	return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_io_write32(uacpi_handle handle, uacpi_size offset,
				     uacpi_u32 in_value)
{
	uacpi_io_addr addr = (uacpi_io_addr)handle + offset;
	outl(addr, in_value);
	return UACPI_STATUS_OK;
}
#endif

#if 0
#define BUMP_POOL_SIZE (16 * 1024 * 1024)
static uint8_t bump_pool[BUMP_POOL_SIZE];
static size_t bump_offset = 0;

void *uacpi_kernel_alloc(uacpi_size size)
{
	size_t old_offset =
		__atomic_fetch_add(&bump_offset, size, __ATOMIC_RELAXED);
	if (old_offset + size > BUMP_POOL_SIZE)
		return NULL; /* out of memory */
	return &bump_pool[old_offset];
	//return kmalloc(size);
}

#include <string.h>

void *uacpi_kernel_alloc_zeroed(uacpi_size size)
{
	void *p = uacpi_kernel_alloc(size);
	if (p)
		memset(p, 0, size);
	return p;
	//return kzalloc(size);
}

void uacpi_kernel_free(void *mem)
{
	(void)mem;
}
#else
void *uacpi_kernel_alloc(uacpi_size size)
{
	return kmalloc(size);
}

#include <string.h>

void *uacpi_kernel_alloc_zeroed(uacpi_size size)
{
	return kzalloc(size);
}

void uacpi_kernel_free(void *mem)
{
	kfree(mem, 0);
}
#endif

uacpi_u64 uacpi_kernel_get_nanoseconds_since_boot(void)
{
	return plat_getnanos();
}

void uacpi_kernel_stall(uacpi_u8 usec)
{
	// ns
	usec *= 1000;

	uint64_t deadline = plat_getnanos() + usec;
	while (plat_getnanos() < deadline) {
		busyloop_hint();
	}
}

void uacpi_kernel_sleep(uacpi_u64 msec)
{
	ksleep(msec * 1000 * 1000);
}

uacpi_handle uacpi_kernel_create_mutex(void)
{
	struct kmutex *mutex = kmalloc(sizeof(struct kmutex));
	kmutex_init(mutex, "uacpi_mutex");
	return mutex;
}

void uacpi_kernel_free_mutex(uacpi_handle handle)
{
	kfree(handle, sizeof(struct kmutex));
}

uacpi_status uacpi_kernel_acquire_mutex(uacpi_handle handle,
					uacpi_u16 ms_timeout)
{
	nstime_t timeout;
	if (ms_timeout == 0xFFFF) {
		timeout = TIMEOUT_INFINITE;
	} else if (ms_timeout == 0) {
		return IS_OK(kmutex_acquire_polling(handle, POLL_ONCE)) ?
			       UACPI_STATUS_OK :
			       UACPI_STATUS_TIMEOUT;
	} else {
		timeout = MSTIME(ms_timeout);
	}

	return IS_OK(kmutex_acquire(handle, timeout)) ? UACPI_STATUS_OK :
							UACPI_STATUS_TIMEOUT;
}

void uacpi_kernel_release_mutex(uacpi_handle handle)
{
	kmutex_release(handle);
}

uacpi_handle uacpi_kernel_create_event(void)
{
	struct semaphore *sem = kmalloc(sizeof(struct semaphore));
	semaphore_init(sem, 0);
	return sem;
}

void uacpi_kernel_free_event(uacpi_handle handle)
{
	kfree(handle, sizeof(struct semaphore));
}

uacpi_bool uacpi_kernel_wait_for_event(uacpi_handle handle,
				       uacpi_u16 ms_timeout)
{
	status_t rv;

	nstime_t timeout;
	if (ms_timeout == 0) {
		rv = sched_wait(handle, WAIT_MODE_POLL, POLL_ONCE);
		goto exit;
	} else if (ms_timeout == 0xFFFF) {
		timeout = TIMEOUT_INFINITE;
	} else {
		timeout = MSTIME(ms_timeout);
	}

	rv = sched_wait(handle, WAIT_MODE_BLOCK, timeout);

exit:
	if (IS_OK(rv)) {
		return UACPI_STATUS_OK;
	}

	return UACPI_STATUS_TIMEOUT;
}

void uacpi_kernel_signal_event(uacpi_handle handle)
{
	semaphore_signal(handle);
}

void uacpi_kernel_reset_event(uacpi_handle handle)
{
	semaphore_reset(handle);
}

uacpi_thread_id uacpi_kernel_get_thread_id(void)
{
	return (void *)curthread();
}

uacpi_status uacpi_kernel_handle_firmware_request(uacpi_firmware_request *req)
{
	panic("firmware request??");
}

uacpi_handle uacpi_kernel_create_spinlock(void)
{
	struct spinlock *sl = kmalloc(sizeof(struct spinlock));
	spinlock_init(sl);
	return sl;
}

void uacpi_kernel_free_spinlock(uacpi_handle handle)
{
	kfree(handle, sizeof(struct spinlock));
}

uacpi_cpu_flags uacpi_kernel_lock_spinlock(uacpi_handle handle)
{
	return spinlock_lock_interrupts(handle);
}

void uacpi_kernel_unlock_spinlock(uacpi_handle handle, uacpi_cpu_flags state)
{
	return spinlock_unlock_interrupts(handle, state);
}

#define STUB() pr_warn("stub %s called\n", __func__);

uacpi_status uacpi_kernel_pci_device_open(uacpi_pci_address address,
					  uacpi_handle *out_handle)
{
	uacpi_pci_address *addr = kmalloc(sizeof(uacpi_pci_address));
	*addr = address;
	*out_handle = addr;
	return UACPI_STATUS_OK;
}

void uacpi_kernel_pci_device_close(uacpi_handle handle)
{
	kfree(handle, sizeof(uacpi_pci_address));
}

uacpi_status uacpi_kernel_pci_read8(uacpi_handle device, uacpi_size offset,
				    uacpi_u8 *value)
{
	uacpi_pci_address *addr = device;
	*value = pci_read8(addr->segment, addr->bus, addr->device,
			   addr->function, offset);
	return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_pci_read16(uacpi_handle device, uacpi_size offset,
				     uacpi_u16 *value)
{
	uacpi_pci_address *addr = device;
	*value = pci_read16(addr->segment, addr->bus, addr->device,
			    addr->function, offset);
	return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_pci_read32(uacpi_handle device, uacpi_size offset,
				     uacpi_u32 *value)
{
	uacpi_pci_address *addr = device;
	*value = pci_read32(addr->segment, addr->bus, addr->device,
			    addr->function, offset);
	return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_pci_write8(uacpi_handle device, uacpi_size offset,
				     uacpi_u8 value)
{
	uacpi_pci_address *addr = device;
	pci_write8(addr->segment, addr->bus, addr->device, addr->function,
		   offset, value);
	return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_pci_write16(uacpi_handle device, uacpi_size offset,
				      uacpi_u16 value)
{
	uacpi_pci_address *addr = device;
	pci_write16(addr->segment, addr->bus, addr->device, addr->function,
		    offset, value);
	return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_pci_write32(uacpi_handle device, uacpi_size offset,
				      uacpi_u32 value)
{
	uacpi_pci_address *addr = device;
	pci_write32(addr->segment, addr->bus, addr->device, addr->function,
		    offset, value);
	return UACPI_STATUS_OK;
}

struct uacpi_irqobj {
	struct irq_object obj;
	uacpi_interrupt_handler handler;
	uacpi_handle uacpi_ctx;
};

static int handle_uacpi_interrupt(void *private)
{
	struct uacpi_irqobj *ctx = private;
	return ctx->handler(ctx->uacpi_ctx) == UACPI_INTERRUPT_HANDLED ?
		       IRQ_ACK :
		       IRQ_NACK;
}

uacpi_status uacpi_kernel_install_interrupt_handler(
	uacpi_u32 irq, uacpi_interrupt_handler handler, uacpi_handle ctx,
	uacpi_handle *out_irq_handle)
{
	struct uacpi_irqobj *handle = kmalloc(sizeof(struct uacpi_irqobj));
	handle->handler = handler;
	handle->uacpi_ctx = ctx;

	irq_object_init(&handle->obj, handle_uacpi_interrupt, handle);
	EXPECT(irq_alloc_ipl(&handle->obj, IPL_DEVICE, IRQ_MIN_IPL,
			     PIN_CONFIG_ANY));

	pr_info("glue irq obj vector: %d\n", handle->obj.slot->vector);

	// hook up in interrupt controller
	arch_program_intr(irq, handle->obj.slot->vector, false);

	*out_irq_handle = handle;
	return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_uninstall_interrupt_handler(uacpi_interrupt_handler,
						      uacpi_handle irq_handle)
{
	STUB();
	return UACPI_STATUS_UNIMPLEMENTED;
}

uacpi_status uacpi_kernel_schedule_work(uacpi_work_type, uacpi_work_handler,
					uacpi_handle ctx)
{
	STUB();
	return UACPI_STATUS_UNIMPLEMENTED;
}

uacpi_status uacpi_kernel_wait_for_work_completion(void)
{
	STUB();
	return UACPI_STATUS_UNIMPLEMENTED;
}

void setup_early_table()
{
	void *buf = (void *)p2v(pmm_alloc_zeroed());
	uacpi_setup_early_table_access(buf, PAGE_SIZE);
}

INIT_STAGE(early_acpi);
INIT_ENTAILS(acpi_early_table, early_acpi);
INIT_DEPS(acpi_early_table, heap_ready_stage);
INIT_NODE(acpi_early_table, setup_early_table);
