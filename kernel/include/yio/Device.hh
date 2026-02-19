#pragma once

#include <yak/queue.h>
#include <yio/TreeNode.hh>
#include <yakpp/Array.hh>

namespace yak::io
{

struct Personality;

class Device : public TreeNode {
	IO_OBJ_DECLARE(Device);

    public:
	virtual void init() override;

	virtual Array *getPersonalities()
	{
		return nullptr;
	}

	virtual Personality *getPersonality()
	{
		return nullptr;
	}

	virtual int probe(Device *provider);
	virtual bool start(Device *provider);
	virtual void stop(Device *provider);

	bool hasDriver = false;
};

class PlatformExpert : public Device {
	IO_OBJ_DECLARE(PlatformExpert);

    public:
	void early_start();
	bool start(Device *provider) override;
};

}
