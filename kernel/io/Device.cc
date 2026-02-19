#include <yio/Device.hh>
#include <nanoprintf.h>

namespace yak::io
{

IO_OBJ_DEFINE_VIRTUAL(Device, TreeNode);
#define super TreeNode

void Device::init()
{
	super::init();
}

int Device::probe([[maybe_unused]] Device *provider)
{
	return 0;
};

bool Device::start([[maybe_unused]] Device *provider)
{
	return true;
};

void Device::stop([[maybe_unused]] Device *provider) {};

}
