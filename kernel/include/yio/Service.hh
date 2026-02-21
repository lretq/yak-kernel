#pragma once

#include <yak/queue.h>
#include <yakpp/Array.hh>
#include <yio/TreeNode.hh>
#include <yio/WorkLoop.hh>

namespace yak::io
{

struct Personality;

class Service : public TreeNode {
	IO_OBJ_DECLARE(Service);

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

	virtual int probe(Service *provider);
	virtual bool start(Service *provider);
	virtual void stop(Service *provider);

	virtual WorkLoop *getWorkLoop();

	Service *getProvider();

	bool hasDriver = false;
};

class PlatformExpert : public Service {
	IO_OBJ_DECLARE(PlatformExpert);

    public:
	void early_start();
	bool start(Service *provider) override;
};

}
