#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include <yak/types.h>

void pci_ecam_init(size_t count);
void pci_ecam_addspace(uint32_t seg, uint32_t bus_start, uint32_t bus_end,
		       paddr_t pa);

#ifdef __cplusplus
}
#endif
