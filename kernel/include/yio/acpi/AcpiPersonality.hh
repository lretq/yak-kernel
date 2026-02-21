#pragma once

#include <yio/Personality.hh>

namespace yak::io
{

struct AcpiPersonality : public Personality {
	IO_OBJ_DECLARE(AcpiPersonality);

    public:
	constexpr bool isEqual(Object *other) const override
	{
		if (auto pers = other->safe_cast<AcpiPersonality>()) {
			if (strcmp(PNP_ID, pers->PNP_ID) == 0)
				return true;
		}
		return false;
	}

	const ClassInfo *getDriverClass() const override
	{
		return clazz;
	}

	constexpr AcpiPersonality(const ClassInfo *clazz, const char *id)
		: PNP_ID(id)
		, clazz(clazz)
	{
	}

	constexpr explicit AcpiPersonality(const char *id)
		: PNP_ID(id)
		, clazz(nullptr)
	{
	}

	void initWithArgs(const char *id)
	{
		init();

		PNP_ID = id;
	}

	const char *PNP_ID = nullptr;
	const ClassInfo *clazz = nullptr;
};

}
