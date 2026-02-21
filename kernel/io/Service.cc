#include <yio/Service.hh>

namespace yak::io
{

IO_OBJ_DEFINE_VIRTUAL(Service, TreeNode);
#define super TreeNode

void Service::init()
{
	super::init();
}

int Service::probe([[maybe_unused]] Service *provider)
{
	return 0;
};

bool Service::start([[maybe_unused]] Service *provider)
{
	return true;
};

void Service::stop([[maybe_unused]] Service *provider) {};

Service *Service::getProvider()
{
	auto parent = super::getParent();
	assert(!parent || parent->isKindOf(Service::getClassInfo()));
	return static_cast<Service *>(parent);
}

WorkLoop *Service::getWorkLoop()
{
	if (auto provider = getProvider())
		return provider->getWorkLoop();
	else
		return nullptr;
}

}
