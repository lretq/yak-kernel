#include <yak/softint.h>
#include <yak/ipl.h>
#include <yak/cpudata.h>

#include "asm.h"

#if CONFIG_LAZY_IPL
ipl_t curipl()
{
	return curcpu().soft_ipl;
}

ipl_t ripl(ipl_t ipl)
{
	ipl_t old = curcpu().soft_ipl;
	curcpu().soft_ipl = ipl;
	return old;
}

void xipl(ipl_t ipl)
{
	curcpu().soft_ipl = ipl;
	if (curcpu().hw_ipl > ipl) {
		curcpu().hw_ipl = ipl;
		write_cr8(ipl);
	}
	if (curcpu().softint_pending >> ipl) {
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
	if (curcpu().softint_pending >> ipl) {
		softint_dispatch(ipl);
	}
}

void setipl(ipl_t ipl)
{
	write_cr8(ipl);
}
#endif
