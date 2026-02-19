#pragma once

#include <stdint.h>
#include <yio/Device.hh>

namespace yak::io
{

struct PciCoordinates {
	uint32_t segment, bus, slot, function;
};

void pci_enumerate(Device *provider);

}
