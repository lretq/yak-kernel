#include <stdint.h>
#include <yio/pci.h>
#include <yak/init.h>

INIT_STAGE(pci_access);

pci_read_fn plat_pci_read32;
pci_write_fn plat_pci_write32;

uint32_t pci_read32(uint32_t segment, uint32_t bus, uint32_t slot,
		    uint32_t function, uint32_t offset)
{
	return plat_pci_read32(segment, bus, slot, function, offset);
}

uint16_t pci_read16(uint32_t segment, uint32_t bus, uint32_t slot,
		    uint32_t function, uint32_t offset)
{
	return (plat_pci_read32(segment, bus, slot, function, offset) >>
		((offset & 0b11) * 8)) &
	       0xffff;
}

uint8_t pci_read8(uint32_t segment, uint32_t bus, uint32_t slot,
		  uint32_t function, uint32_t offset)
{
	return (plat_pci_read32(segment, bus, slot, function, offset) >>
		((offset & 0b11) * 8)) &
	       0xff;
}

void pci_write8(uint32_t seg, uint32_t bus, uint32_t slot, uint32_t function,
		uint32_t offset, uint8_t value)
{
	uint32_t old = plat_pci_read32(seg, bus, slot, function, offset);
	int bitoffset = 8 * (offset & 0b11);
	old &= ~(0xff << bitoffset);
	old |= value << bitoffset;
	plat_pci_write32(seg, bus, slot, function, offset, old);
}

void pci_write16(uint32_t seg, uint32_t bus, uint32_t slot, uint32_t function,
		 uint32_t offset, uint16_t value)
{
	uint32_t old = plat_pci_read32(seg, bus, slot, function, offset);
	int bitoffset = 8 * (offset & 0b11);
	old &= ~(0xffff << bitoffset);
	old |= value << bitoffset;
	plat_pci_write32(seg, bus, slot, function, offset, old);
}

void pci_write32(uint32_t seg, uint32_t bus, uint32_t slot, uint32_t function,
		 uint32_t offset, uint32_t value)
{
	plat_pci_write32(seg, bus, slot, function, offset, value);
}
