#include <yak/log.h>
#include <uacpi/utilities.h>
#include <yakpp/Array.hh>
#include <yio/Service.hh>
#include <yakpp/Object.hh>
#include <yio/IoRegistry.hh>
#include <yio/acpi/AcpiDevice.hh>
#include <yio/acpi/AcpiPersonality.hh>
#include <uacpi/resources.h>
#include <nanoprintf.h>

namespace yak::io
{

IO_OBJ_DEFINE(AcpiDevice, Service);
#define super Service

void AcpiDevice::initWithArgs(uacpi_namespace_node *node)
{
	init();

	node_ = node;

	uacpi_status ret = uacpi_get_namespace_node_info(node, &node_info_);
	if (uacpi_unlikely_error(ret)) {
		const char *path =
			uacpi_namespace_node_generate_absolute_path(node);
		pr_error("unable to retrieve node %s information: %s", path,
			 uacpi_status_to_string(ret));
		uacpi_free_absolute_path(path);
		return;
	}

	if (node_info_->flags & UACPI_NS_NODE_INFO_HAS_HID) {
		auto p = IO_OBJ_CREATE(AcpiPersonality);
		p->initWithArgs(node_info_->hid.value);
		personalities_->push_back(p);
		p->release();

		char buf[32];
		npf_snprintf(buf, sizeof(buf), "AcpiDevice[HID:%s]",
			     node_info_->hid.value);
		name = String::fromCStr(buf);
	}

	if (node_info_->flags & UACPI_NS_NODE_INFO_HAS_CID) {
		for (size_t i = 0; i < node_info_->cid.num_ids; i++) {
			auto p = IO_OBJ_CREATE(AcpiPersonality);
			p->initWithArgs(node_info_->cid.ids[i].value);
			personalities_->push_back(p);
			p->release();
		}
	}
}

void AcpiDevice::deinit()
{
	personalities_->release();
	uacpi_free_namespace_node_info(node_info_);
}

void AcpiDevice::init()
{
	super::init();
	ALLOC_INIT(personalities_, Array);
}

Array *AcpiDevice::getPersonalities()
{
	return personalities_;
}

}
