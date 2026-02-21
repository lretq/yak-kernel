#pragma once

#include <stdint.h>
#include <yio/Service.hh>

namespace yak::io
{

struct PciCoordinates {
	uint32_t segment, bus, slot, function;
};

void pci_enumerate(Service *provider);

}
