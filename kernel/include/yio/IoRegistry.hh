#pragma once

#include <yak/queue.h>
#include <yio/Device.hh>
#include <yio/TreeNode.hh>
#include <yakpp/Object.hh>
#include <yakpp/Mutex.hh>

namespace yak::io
{

class IoRegistry : public Object {
	IO_OBJ_DECLARE(IoRegistry);

    private:
	void init() override;

    public:
	static IoRegistry &getRegistry();

	Device &getExpert();

	// match with registered personalities
	const ClassInfo *match(Device *provider, Personality &personality);

	void matchAll(TreeNode *node = nullptr);

	void registerPersonality(Personality *personality);

	void dumpTree();

    private:
	Mutex mutex_;
	TAILQ_HEAD(personality_list, Personality) personalities_;
	PlatformExpert platform_expert;
};

}
