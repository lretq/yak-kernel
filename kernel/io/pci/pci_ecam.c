#include <yak/heap.h>
#include <yak/log.h>
#include <yak/vm/map.h>
#include <yak/types.h>
#include <yio/pci.h>

#define MCFG_MAPPING_SIZE(buscount) (4096l * 8l * 32l * (buscount))

struct ecam_space {
	uint32_t segment;
	uint32_t start_bus, end_bus;
	// mapped address
	vaddr_t address;
};

static struct ecam_space *ecam_entries;
static size_t ecam_entrycount;

static struct ecam_space *ecam_bus(uint32_t segment, uint32_t bus)
{
	for (size_t i = 0; i < ecam_entrycount; i++) {
		if (ecam_entries[i].segment == segment &&
		    bus >= ecam_entries[i].start_bus &&
		    bus <= ecam_entries[i].end_bus) {
			return &ecam_entries[i];
		}
	}
	return NULL;
}

static uint32_t pci_ecam_read(uint32_t segment, uint32_t bus, uint32_t slot,
			      uint32_t function, uint32_t offset)
{
	struct ecam_space *entry = ecam_bus(segment, bus);
	if (!entry) {
		pr_warn("no ecam entry for %d:%d\n", segment, bus);
		return 0xFFFFFFFF;
	}

	volatile uint32_t *addr =
		(volatile uint32_t *)(entry->address +(
					      ((bus - entry->start_bus) << 20 |
					       slot << 15 | function << 12) |
				      (offset & ~0x3)));
	return *addr;
}

static void pci_ecam_write(uint32_t segment, uint32_t bus, uint32_t slot,
			   uint32_t function, uint32_t offset, uint32_t value)
{
	struct ecam_space *entry = ecam_bus(segment, bus);
	if (!entry) {
		pr_warn("no ecam entry for %d:%d\n", segment, bus);
		return;
	}

	volatile uint32_t *addr =
		(volatile uint32_t *)(entry->address +
					      (((bus - entry->start_bus) << 20 |
					       slot << 15 | function << 12) |
				      (offset & ~0x3)));

	*addr = value;
}

void pci_ecam_init(size_t count)
{
	ecam_entries = kcalloc(count, sizeof(struct ecam_space));
	ecam_entrycount = 0;
	plat_pci_read32 = pci_ecam_read;
	plat_pci_write32 = pci_ecam_write;
}

void pci_ecam_addspace(uint32_t seg, uint32_t bus_start, uint32_t bus_end,
		       paddr_t pa)
{
	struct ecam_space *entry = &ecam_entries[ecam_entrycount++];
	entry->segment = seg;
	entry->start_bus = bus_start;
	entry->end_bus = bus_end;
	size_t mapsz = MCFG_MAPPING_SIZE(bus_end - bus_start);
	EXPECT(vm_map_mmio(kmap(), pa, mapsz, VM_RW, VM_CACHE_DISABLE,
			   &entry->address));
	pr_debug("pci_ecam space %d:<%d-%d> mapped to 0x%lx\n", seg, bus_start,
		 bus_end, entry->address);
}
