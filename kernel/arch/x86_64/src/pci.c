#include <stdint.h>
#include <uacpi/acpi.h>
#include <uacpi/tables.h>
#include <yak/log.h>
#include <yio/pci.h>
#include <yak/init.h>
#include <yio/pci_ecam.h>
#include <assert.h>

#include "asm.h"

enum {
	LEGACY_PCI_CONFIG_ADDR = 0xCF8,
	LEGACY_PCI_CONFIG_DATA = 0xCFC,
};

static uint32_t pci_legacy_read([[maybe_unused]] uint32_t segment, uint32_t bus,
				uint32_t slot, uint32_t function,
				uint32_t offset)
{
	assert(segment == 0);
	assert(bus < 256 && slot < 32 && function < 8 && offset < 256);
	assert(!(offset & 0b11));
	uint32_t address = (1 << 31) // enable bit
			   | (bus << 16) | (slot << 11) | (function << 8) |
			   (offset & ~(uint32_t)3);

	outl(LEGACY_PCI_CONFIG_ADDR, address);
	return inl(LEGACY_PCI_CONFIG_DATA);
}

static void pci_legacy_write([[maybe_unused]] uint32_t segment, uint32_t bus,
			     uint32_t slot, uint32_t function, uint32_t offset,
			     uint32_t value)
{
	assert(segment == 0);
	assert(bus < 256 && slot < 32 && function < 8 && offset < 256);
	assert(!(offset & 0b11));
	uint32_t address = (1 << 31) // enable bit
			   | (bus << 16) | (slot << 11) | (function << 8) |
			   (offset & ~(uint32_t)3);

	outl(LEGACY_PCI_CONFIG_ADDR, address);
	outl(LEGACY_PCI_CONFIG_DATA, value);
}

void x86_pci_init()
{
	uacpi_table tbl;
	int ret = uacpi_table_find_by_signature("MCFG", &tbl);
	if (ret == UACPI_STATUS_OK) {
		pr_info("using pci_ecam PCI access\n");

		struct acpi_mcfg *mcfg = tbl.ptr;
		size_t mcfg_entrycount =
			(mcfg->hdr.length - sizeof(mcfg->hdr) - (mcfg->rsvd)) /
			sizeof(struct acpi_mcfg_allocation);
		pci_ecam_init(mcfg_entrycount);

		for (size_t i = 0; i < mcfg_entrycount; i++) {
			struct acpi_mcfg_allocation *entry = &mcfg->entries[i];
			pci_ecam_addspace(entry->segment, entry->start_bus,
					  entry->end_bus, entry->address);
		}

		uacpi_table_unref(&tbl);
	} else {
		pr_info("using legacy PCI access\n");

		plat_pci_read32 = pci_legacy_read;
		plat_pci_write32 = pci_legacy_write;
	}
}

INIT_ENTAILS(x86_pci_access, pci_access);
INIT_DEPS(x86_pci_access, early_acpi_stage);
INIT_NODE(x86_pci_access, x86_pci_init);
