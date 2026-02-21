#pragma once

#include <yio/Service.hh>
#include <stdint.h>

namespace yak::io
{

struct PciBus : public Service {
	IO_OBJ_DECLARE(PciBus);

    public:
	void init() override;

	void initWithArgs(uint32_t segId, uint32_t busId, uint32_t seg,
			  uint32_t bus, uint32_t slot, uint32_t function,
			  PciBus *parentBus = nullptr);

	bool start(Service *provider) override;

	PciBus *parentBus;

	uint32_t segId, busId;

	uint32_t seg, bus, slot, function;
};

}
