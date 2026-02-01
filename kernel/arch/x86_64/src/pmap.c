#include <yak/vm/pmap.h>
#include "asm.h"

void pmap_activate(struct pmap *pmap)
{
	/* This was moved to the generic map_activate
	if (read_cr3() == pmap->top_level)
		return;
	*/
	write_cr3(pmap->top_level);
}
