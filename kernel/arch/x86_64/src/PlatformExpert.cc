#include <cstdint>
#include <nanoprintf.h>
#include <uacpi/tables.h>
#include <uacpi/acpi.h>
#include <uacpi/uacpi.h>
#include <uacpi/resources.h>
#include <uacpi/utilities.h>
#include <yak/log.h>
#include <yak/init.h>
#include <yak/irq.h>
#include <yak/vm/map.h>
#include <yio/pci/Pci.hh>
#include <yio/acpi/AcpiDevice.hh>
#include <yio/IoRegistry.hh>

namespace yak::io
{

IO_OBJ_DEFINE(PlatformExpert, Device)
#define super Device

static uacpi_iteration_decision
acpi_ns_enum(void *provider, uacpi_namespace_node *node,
	     [[maybe_unused]] uacpi_u32 node_depth)
{
	auto device = IO_OBJ_CREATE(AcpiDevice);
	device->initWithArgs(node);
	((Device *)provider)->attachChild(device);
	device->release();
	return UACPI_ITERATION_DECISION_CONTINUE;
}

struct AcpiNamespace : public Device {
	IO_OBJ_DECLARE(AcpiNamespace);

    public:
	bool start(Device *provider) override
	{
		if (!Device::start(provider))
			return false;

		uacpi_namespace_for_each_child(uacpi_namespace_root(),
					       acpi_ns_enum, UACPI_NULL,
					       UACPI_OBJECT_DEVICE_BIT,
					       UACPI_MAX_DEPTH_ANY, this);

		return true;
	}
};

IO_OBJ_DEFINE(AcpiNamespace, Device);

struct interrupt_override isr_overrides[16];

struct IOApic : public Device {
	IO_OBJ_DECLARE(IOApic);

    private:
	static constexpr size_t IOAPIC_VER = 0x01;
	static constexpr size_t IOAPIC_REDTBL_BASE = 0x10;

    public:
	void initWithArgs(uint8_t id, uint32_t gsi_base, paddr_t addr);

	void program(uint8_t irq, irq_vec_t vector, uint8_t cpu, bool low,
		     bool edge, bool masked)
	{
		vector += 32;

		auto gsi = irq - gsiBase_;

		uint64_t val = vector;
		// delivery mode = fixed
		val |= (0b000 << 8);
		// destination mode = phyiscal
		val |= 0 << 11;
		// pin polarity
		// 0 => active high
		// 1 => active low
		val |= (low ? 1ULL : 0ULL) << 13;
		// trigger mode
		// 0 => edge
		// 1 => level
		val |= (!edge ? 1ULL : 0ULL) << 15;
		// mask
		val |= masked << 16;
		// destination
		val |= (uint64_t)cpu << 56;

		write(IOAPIC_REDTBL_BASE + gsi * 2, (uint32_t)val);
		write(IOAPIC_REDTBL_BASE + gsi * 2 + 1, (uint32_t)(val >> 32));
	}

    private:
	inline void write(uint8_t offset, uint32_t value)
	{
		*reinterpret_cast<volatile uint32_t *>(base_) = offset;
		*reinterpret_cast<volatile uint32_t *>(base_ + 0x10) = value;
	}

	inline uint32_t read(uint8_t offset) const
	{
		*reinterpret_cast<volatile uint32_t *>(base_) = offset;
		return *reinterpret_cast<volatile uint32_t *>(base_ + 0x10);
	}

	uint8_t id_;

	uint8_t ver_;

    public:
	uint8_t maxRedirEnt_;

	uint8_t gsiBase_;

    private:
	vaddr_t base_;
};

IO_OBJ_DEFINE(IOApic, Device);

IOApic *ioapics[8] = { nullptr };
size_t ioapic_count = 0;

struct CpuNamespace : public Device {
	IO_OBJ_DECLARE(CpuNamespace);

    private:
	static void parse_gsi(struct acpi_entry_hdr *item,
			      [[maybe_unused]] void *arg)
	{
		acpi_madt_interrupt_source_override *ent =
			reinterpret_cast<acpi_madt_interrupt_source_override *>(
				item);
		assert(ent);
		assert(ent->source < 16);
		struct interrupt_override *ovr = &isr_overrides[ent->source];
		ovr->gsi = ent->gsi;
		ovr->edge = (ent->flags & 0x8) != 0;
		ovr->low = (ent->flags & 0x2) != 0;
	}

	static void parse_ioapic(struct acpi_entry_hdr *item, void *arg)
	{
		acpi_madt_ioapic *ent =
			reinterpret_cast<acpi_madt_ioapic *>(item);
		Device *provider = static_cast<Device *>(arg);

		auto ioapic = IO_OBJ_CREATE(IOApic);
		ioapic->initWithArgs(ent->id, ent->gsi_base, ent->address);

		ioapics[ioapic_count++] = ioapic;
		provider->attachChildAndUnref(ioapic);
	}

	static void madt_walk(struct acpi_madt *madt,
			      void (*callback)(struct acpi_entry_hdr *item,
					       void *arg),
			      void *arg, uint8_t entry_type)
	{
		for (char *item = (char *)&madt->entries[0];
		     item < ((char *)madt->entries +
			     (madt->hdr.length - sizeof(struct acpi_madt)));) {
			struct acpi_entry_hdr *header =
				(struct acpi_entry_hdr *)item;
			if (header->type == entry_type)
				callback(header, arg);
			item += header->length;
		}
	}

    public:
	bool start(Device *provider) override
	{
		if (!Device::start(provider))
			return false;

		uacpi_table tbl;
		uacpi_status ret = uacpi_table_find_by_signature("APIC", &tbl);
		if (uacpi_unlikely_error(ret)) {
			panic("missing MADT\n");
		}

		acpi_madt *madt = reinterpret_cast<acpi_madt *>(tbl.ptr);

		for (int i = 0; i < 16; i++) {
			isr_overrides[i].gsi = i;
			isr_overrides[i].low = false;
			isr_overrides[i].edge = false;
		}

		madt_walk(madt, parse_gsi, NULL,
			  ACPI_MADT_ENTRY_TYPE_INTERRUPT_SOURCE_OVERRIDE);
		madt_walk(madt, parse_ioapic, this,
			  ACPI_MADT_ENTRY_TYPE_IOAPIC);

		uacpi_table_unref(&tbl);

		return true;
	}
};

IO_OBJ_DEFINE(CpuNamespace, Device);

extern "C" void arch_program_intr(uint8_t irq, irq_vec_t vector, int masked)
{
	bool low = false;
	bool edge = true;
	if (irq < 16) {
		auto override = &isr_overrides[irq];
		edge = override->edge;
		low = override->low;
		irq = override->gsi;
	}

	for (size_t i = 0; i < ioapic_count; i++) {
		if (i >= ioapics[i]->gsiBase_ &&
		    i <= ioapics[i]->gsiBase_ + ioapics[i]->maxRedirEnt_) {
			auto ioapic = ioapics[i];
			ioapic->program(irq, vector, 0, low, edge, masked);
			return;
		}
	}

	panic("couldnt find ioapic\n");
}

void IOApic::initWithArgs(uint8_t id, uint32_t gsi_base, paddr_t addr)
{
	init();

	EXPECT(vm_map_mmio(kmap(), addr, PAGE_SIZE, VM_RW, VM_CACHE_DISABLE,
			   &base_));

	id_ = id;

	uint32_t reg;
	reg = read(IOAPIC_VER);
	ver_ = (uint8_t)reg;
	maxRedirEnt_ = ((reg >> 16) & 0xFF) + 1;
	gsiBase_ = gsi_base;

	pr_info("IOApic: id #%d, range %d-%d\n", id, gsiBase_,
		gsiBase_ + maxRedirEnt_);

	char buf[32];
	npf_snprintf(buf, sizeof(buf), "IOApic[@%d]", id);
	name = String::fromCStr(buf);
}

void PlatformExpert::early_start()
{
	CpuNamespace *cpu;
	ALLOC_INIT(cpu, CpuNamespace);
	cpu->start(this);
	this->attachChildAndUnref(cpu);
}

extern "C" void c_expert_early_start()
{
	auto &reg = IoRegistry::getRegistry();
	reg.getExpert().safe_cast<PlatformExpert>()->early_start();
}

INIT_ENTAILS(early_expert, early_io, bsp_ready);
INIT_DEPS(early_expert, early_acpi_stage);
INIT_NODE(early_expert, c_expert_early_start);

bool PlatformExpert::start(Device *provider)
{
	if (!Device::start(provider))
		return false;

	AcpiNamespace *acpi_root_dev;
	ALLOC_INIT(acpi_root_dev, AcpiNamespace);
	this->attachChild(acpi_root_dev);
	acpi_root_dev->start(this);
	acpi_root_dev->release();

	pci_enumerate(this);

	return true;
}

void expert_start()
{
	auto &reg = IoRegistry::getRegistry();
	pr_info("starting PlatformExpert\n");
	reg.getExpert().start(nullptr);
}

INIT_ENTAILS(expert, io);
INIT_DEPS(expert, acpi_stage);
INIT_NODE(expert, expert_start);

}
