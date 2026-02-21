#pragma once

#include <yio/Personality.hh>

namespace yak::io
{

class PciPersonality : public Personality {
	IO_OBJ_DECLARE(PciPersonality);

    public:
	static constexpr long MATCH_ANY = -1;

	constexpr PciPersonality(const ClassInfo *driver, long vendor,
				 long device_id, long classCode, long subclass)
		: driver(driver)
		, vendor(vendor)
		, device(device_id)
		, classCode(classCode)
		, subclass(subclass)
	{
	}

	constexpr bool isEqual(Object *other) const override
	{
		if (auto pers = other->safe_cast<PciPersonality>()) {
			if (pers->vendor != MATCH_ANY &&
			    this->vendor != MATCH_ANY &&
			    pers->vendor != this->vendor)
				return false;

			if (pers->device != MATCH_ANY &&
			    this->device != MATCH_ANY &&
			    pers->device != this->device)
				return false;

			if (pers->classCode != MATCH_ANY &&
			    this->classCode != MATCH_ANY &&
			    pers->classCode != this->classCode)
				return false;

			if (pers->subclass != MATCH_ANY &&
			    this->subclass != MATCH_ANY &&
			    pers->subclass != this->subclass)
				return false;

			return true;
		}
		return false;
	}

	constexpr const ClassInfo *getDriverClass() const override
	{
		return driver;
	}

    private:
	const ClassInfo *driver = nullptr;
	long vendor = MATCH_ANY;
	long device = MATCH_ANY;
	long classCode = MATCH_ANY;
	long subclass = MATCH_ANY;
};

}
