#include <yak/percpu.h>
#include <yak/softint.h>
#include <yak/ipl.h>
#include <yak/cpudata.h>

#include "asm.h"

#if CONFIG_LAZY_IPL
ipl_t curipl()
{
	return PERCPU_FIELD_LOAD(soft_ipl);
}

ipl_t ripl(ipl_t ipl)
{
	return PERCPU_FIELD_XCHG(soft_ipl, ipl);

	/*
	ipl_t old = curcpu()->soft_ipl;
	curcpu()->soft_ipl = ipl;
	return old;
	*/
}

void xipl(ipl_t ipl)
{
	PERCPU_FIELD_STORE(soft_ipl, ipl);
	if (PERCPU_FIELD_LOAD(hw_ipl) > ipl) {
		PERCPU_FIELD_STORE(hw_ipl, ipl);
		write_cr8(ipl);
	}
	if (PERCPU_FIELD_LOAD(softint_pending) >> ipl) {
		softint_dispatch(ipl);
	}
}
#else
ipl_t curipl()
{
	return read_cr8();
}

ipl_t ripl(ipl_t ipl)
{
	ipl_t old = read_cr8();
	if (ipl == old)
		return old;
	assert(ipl >= old);
	write_cr8(ipl);
	return old;
}

void xipl(ipl_t ipl)
{
	[[maybe_unused]]
	ipl_t old = curipl();
	if (ipl == old)
		return;
	assert(ipl <= old);
	write_cr8(ipl);
	if (PERCPU_FIELD_LOAD(softint_pending) >> ipl) {
		softint_dispatch(ipl);
	}
}

void setipl(ipl_t ipl)
{
	write_cr8(ipl);
}
#endif
