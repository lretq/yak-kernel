#define pr_fmt(fmt) "PciBus: " fmt

#include <yio/pci/PciBus.hh>
#include <yio/pci/PciDevice.hh>
#include <yak/log.h>
#include <nanoprintf.h>

#define PCI_UTILS
#include "pci-utils.h"

namespace yak::io
{

IO_OBJ_DEFINE(PciBus, Device);
#define super Device

void PciBus::init()
{
	super::init();
}

void PciBus::initWithArgs(uint32_t segId, uint32_t busId, uint32_t seg,
			  uint32_t bus, uint32_t slot, uint32_t function,
			  PciBus *parentBus)
{
	super::init();

	this->segId = segId;
	this->busId = busId;
	this->seg = seg;
	this->bus = bus;
	this->slot = slot;
	this->function = function;
	this->parentBus = parentBus;

	char buf[32];
	npf_snprintf(buf, sizeof(buf), "PciBus[%d-%d]", segId, busId);
	name = String::fromCStr(buf);
}

namespace
{

void fn_enum(PciBus *provider, PCIARGS)
{
	auto cc = classCode(IPCIARGS);
	auto sc = subClass(IPCIARGS);

	if ((cc == 0x6) && (sc == 0x0))
		return;

	if ((cc == 0x6) && (sc == 0x4)) {
		// == bus
		PciBus *busDev = IO_OBJ_CREATE(PciBus);
		busDev->initWithArgs(0, secondaryBus(IPCIARGS), IPCIARGS,
				     provider);
		busDev->start(provider);

		provider->attachChild(busDev);

		return;
	}

	assert((headerType(IPCIARGS) & ~0x80) == 0x0);

	PciDevice *dev = IO_OBJ_CREATE(PciDevice);
	dev->initWithPci(IPCIARGS);

#if 0
	pr_debug("enum pci function at %d-%d.%02d@%d\n", segment, bus, slot,
		function);
#endif
	provider->attachChild(dev);
}

void dev_enum(PciBus *provider, uint32_t segment, uint32_t bus, uint32_t slot)
{
	uint16_t vid;
	uint8_t function = 0;

	vid = vendorId(IPCIARGS);
	if (vid == 0xFFFF) {
		return;
	}

	fn_enum(provider, IPCIARGS);
	if ((headerType(IPCIARGS) & 0x80) != 0) {
		for (function = 1; function < 8; function++) {
			if (vendorId(IPCIARGS) == 0xFFFF)
				continue;
			fn_enum(provider, IPCIARGS);
		}
	}
}

}

bool PciBus::start(Device *provider)
{
	if (!Device::start(provider))
		return false;

#if 0
	pr_debug("enumerate bus %d-%d\n", seg, bus);
#endif

	for (uint8_t device = 0; device < 32; device++) {
		dev_enum(this, segId, busId, device);
	}

	return true;
}

void pci_enumerate(Device *provider)
{
	auto header = headerType(0, 0, 0, 0);
	if ((header & 0x80) == 0) {
		PciBus *dev = IO_OBJ_CREATE(PciBus);
		dev->initWithArgs(0, 0, 0, 0, 0, 0);

		provider->attachChild(dev);

		dev->start(provider);

		dev->release();

	} else {
		for (int function = 0; function < 8; function++) {
			if (vendorId(0, 0, 0, function) == 0xFFFF)
				continue;

			PciBus *dev = IO_OBJ_CREATE(PciBus);

			dev->initWithArgs(0, function, 0, 0, 0, function);

			provider->attachChild(dev);

			dev->start(provider);

			dev->release();
		}
	}
}

}
