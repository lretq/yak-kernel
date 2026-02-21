#include <stdint.h>
#include <nanoprintf.h>
#include <yio/pci.h>
#include <yio/pci/Pci.hh>
#include <yak/log.h>
#include <yio/pci/PciBus.hh>
#include <yio/pci/PciDevice.hh>

#define PCI_UTILS
#include "pci-utils.h"

namespace yak::io
{

IO_OBJ_DEFINE(PciDevice, Service);
#define super Device

void PciDevice::init()
{
	Service::init();
}

void PciDevice::initWithPci(uint32_t segment, uint32_t bus, uint32_t slot,
			    uint32_t function)
{
	PciDevice::init();

	personality = PciPersonality(nullptr, vendorId(IPCIARGS),
				     deviceId(IPCIARGS), classCode(IPCIARGS),
				     subClass(IPCIARGS));

	coords.segment = segment;
	coords.bus = bus;
	coords.slot = slot;
	coords.function = function;

	char buf[32];
	npf_snprintf(buf, 32, "PciDev[%d-%d.%02d@%d]", segment, bus, slot,
		     function);
	name = String::fromCStr(buf);
}

Personality *PciDevice::getPersonality()
{
	return &personality;
}

}
