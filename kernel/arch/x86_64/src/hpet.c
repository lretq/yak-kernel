#include "yak/clocksource.h"
#include <stdint.h>
#include <uacpi/acpi.h>
#include <uacpi/tables.h>
#include <yak/status.h>
#include <yak/vm/map.h>
#include <yak/types.h>

enum {
	HPET_REG_GENERAL_CAPABILITY = 0x00,
	HPET_REG_GENERAL_CONFIG = 0x10,
	HPET_REG_GENERAL_INT_STATUS = 0x20,
	HPET_REG_COUNTER_MAIN = 0xF0,
};

#define FEMTO 1000000000000000ULL

struct hpet {
	vaddr_t base;

	uint16_t min_clock_ticks;
	uint32_t period;
};

static struct hpet hpet;

static void hpet_write(const uint64_t reg, const uint64_t value)
{
	*(volatile uint64_t *)(hpet.base + reg) = value;
}

static uint64_t hpet_read(const uint64_t reg)
{
	return *(volatile uint64_t *)(hpet.base + reg);
}

static uint64_t hpet_counter(struct clocksource *)
{
	return hpet_read(HPET_REG_COUNTER_MAIN);
}

static void hpet_enable()
{
	hpet_write(HPET_REG_GENERAL_CONFIG,
		   hpet_read(HPET_REG_GENERAL_CONFIG) | 0b1);
}

static void hpet_disable()
{
	hpet_write(HPET_REG_GENERAL_CONFIG,
		   hpet_read(HPET_REG_GENERAL_CONFIG) & ~(0b1));
}

static bool hpet_probe()
{
	uacpi_table tbl;
	if (uacpi_table_find_by_signature("HPET", &tbl) != UACPI_STATUS_OK)
		return false;
	uacpi_table_unref(&tbl);
	return true;
}

static status_t hpet_setup(struct clocksource *clock)
{
	uacpi_table tbl;
	if (uacpi_table_find_by_signature("HPET", &tbl) != UACPI_STATUS_OK)
		return YAK_NOT_SUPPORTED;

	struct acpi_hpet *hpet_table = tbl.ptr;

	EXPECT(vm_map_mmio(kmap(), hpet_table->address.address, PAGE_SIZE,
			   VM_RW, VM_CACHE_DISABLE, &hpet.base));

	hpet.min_clock_ticks = hpet_table->min_clock_tick;
	hpet.period = hpet_read(HPET_REG_GENERAL_CAPABILITY) >> 32;

	hpet_write(HPET_REG_COUNTER_MAIN, 0);
	hpet_enable();

	clock->frequency = FEMTO / hpet.period;

	uacpi_table_unref(&tbl);

	return YAK_SUCCESS;
}

status_t hpet_init(struct clocksource *)
{
	return YAK_SUCCESS;
}

struct clocksource hpet_clocksource = {
	.name = "hpet",
	.quality = 200,
	.frequency = -1,
	.counter = hpet_counter,
	.probe = hpet_probe,
	.setup = hpet_setup,
	.init = hpet_init,
	.flags = CLOCKSOURCE_EARLY,
};

void hpet_register()
{
	clocksource_register(&hpet_clocksource);
}
