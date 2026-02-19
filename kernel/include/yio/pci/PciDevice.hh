#pragma once

#include <yak/queue.h>
#include <yio/pci/PciPersonality.hh>
#include <yio/pci/Pci.hh>

namespace yak::io
{

struct PciDevice : public Device {
	IO_OBJ_DECLARE(PciDevice);

    public:
	Personality *getPersonality() override;

	void init() override;

	void initWithPci(uint32_t segment, uint32_t bus, uint32_t slot,
			 uint32_t function);

    private:
	PciPersonality personality;
	PciCoordinates coords;
};

}
