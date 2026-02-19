#pragma once

#include <yio/Device.hh>
#include <yio/IoRegistry.hh>
#include <yakpp/Array.hh>
#include <uacpi/utilities.h>

namespace yak::io
{

struct AcpiDevice : public Device {
	IO_OBJ_DECLARE(AcpiDevice);

    public:
	Array *getPersonalities() override;

	void init() override;

	void deinit() override;

	void initWithArgs(uacpi_namespace_node *node);

	uacpi_namespace_node *node_ = nullptr;
	uacpi_namespace_node_info *node_info_ = nullptr;

    private:
	Array *personalities_ = nullptr;
};

}
