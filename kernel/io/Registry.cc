#include <yak/log.h>
#include <yak/queue.h>
#include <yio/Service.hh>
#include <yakpp/LockGuard.hh>
#include <yio/Personality.hh>
#include <yio/IoRegistry.hh>

namespace yak::io
{

IO_OBJ_DEFINE(IoRegistry, Object);
#define super Object

void IoRegistry::init()
{
	super::init();
	mutex_.init("IoRegistry");
	TAILQ_INIT(&personalities_);
	platform_expert.init();
}

static bool registry_init = false;

IoRegistry &IoRegistry::getRegistry()
{
	static IoRegistry singleton;
	if (unlikely(!registry_init)) {
		singleton.init();
		registry_init = true;
	}
	return singleton;
}

Service &IoRegistry::getExpert()
{
	return platform_expert;
}

const ClassInfo *IoRegistry::match(Service *provider, Personality &personality)
{
	LockGuard lock(mutex_);
	Personality *elm;
	int highest_probe = -10000000;
	const ClassInfo *best_driver = nullptr;

	TAILQ_FOREACH(elm, &personalities_, list_entry)
	{
		if (!elm->isEqual(&personality))
			continue;

		auto drv = elm->getDriverClass();
		assert(drv);

		auto dev = drv->createInstance()->safe_cast<Service>();
		assert(dev);

		auto score = dev->probe(provider);
		dev->release();

		if (score > highest_probe) {
			highest_probe = score;
			best_driver = drv;
		}
	}

	return best_driver;
}

void IoRegistry::matchAll(TreeNode *node)
{
	// TODO: what if we want to match a bus?
	if (node && node->safe_cast<Service>()->hasDriver)
		return;

	if (node) {
		auto dev = node->safe_cast<Service>();
		assert(dev);
		auto pers = dev->getPersonality();
		const ClassInfo *driverClass = nullptr;
		if (pers) {
			// single personality device
			driverClass = match(dev, *pers);
		} else {
			// multi personality device
			auto persies = dev->getPersonalities();
			if (!persies) {
				// hit some kind of root/bus device
				goto next;
			}
			for (auto ent : *persies) {
				// assume highest priority first
				driverClass = match(
					dev, *ent->safe_cast<Personality>());
				if (driverClass)
					break;
			}
		}

		if (driverClass) {
			pr_debug("found driver %s for device %s\n",
				 driverClass->className, dev->name->getCStr());
			auto driver = driverClass->createInstance()
					      ->safe_cast<Service>();
			driver->init();
			driver->start(dev);
			dev->hasDriver = true;
			dev->attachChildAndUnref(driver);
		}
	} else {
		node = &getExpert();
	}

next:
	TreeNode *elm;
	TAILQ_FOREACH(elm, &node->children_, list_entry_)
	{
		matchAll(elm);
	}
}

void IoRegistry::registerPersonality(Personality *personality)
{
	assert(personality);
	assert(personality->getDriverClass());

	{
		LockGuard lock(mutex_);
		TAILQ_INSERT_TAIL(&this->personalities_, personality,
				  list_entry);
	}

	matchAll();
}

static void print_node(TreeNode *elm)
{
	if (elm->name)
		printk(0, "%s\n", elm->name->getCStr());
	else
		printk(0, "%s\n", elm->getClassInfo()->className);
}

static void dump_level(TreeNode *node, size_t level, uint64_t verticals = 0,
		       bool is_last = false)
{
	for (size_t i = 0; i < level; i++) {
		if (verticals & (1ULL << i))
			printk(0, "│\t");
		else
			printk(0, "\t");
	}

	if (level != 0)
		printk(0, "%s", is_last ? "└── " : "├── ");

	print_node(node);

	TreeNode *elm;
	size_t j = 0;
	TAILQ_FOREACH(elm, &node->children_, list_entry_)
	{
		j++;
		bool child_is_last = (j == node->childcount);
		// Set or clear bit at current level for vertical lines
		uint64_t next_verticals = verticals;
		if (level != 0 && !is_last)
			next_verticals |= (1ULL << level);
		else
			next_verticals &= ~(1ULL << level);
		dump_level(elm, level + 1, next_verticals, child_is_last);
	}
}

void IoRegistry::dumpTree()
{
	LockGuard lock(mutex_);
	printk(0, "=== IoRegistry Dump ===\n");
	dump_level(&platform_expert, 0);
	printk(0, "=== IoRegistry Dump end ===\n");
}

}
