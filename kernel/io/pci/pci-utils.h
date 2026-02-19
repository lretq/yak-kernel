#pragma once

#ifndef PCI_UTILS
#error "unwanted pci-utils.h include"
#endif

#include <stdint.h>
#include <yio/pci.h>

#define PCIARGS uint32_t segment, uint32_t bus, uint32_t slot, uint32_t function
#define IPCIARGS segment, bus, slot, function

static inline uint16_t vendorId(PCIARGS)
{
	auto reg0 = pci_read16(IPCIARGS, 0);
	return (uint16_t)reg0;
}

static inline uint64_t deviceId(PCIARGS)
{
	auto reg0 = pci_read32(IPCIARGS, 0);
	return (uint16_t)(reg0 >> 16);
}

static inline uint8_t classCode(PCIARGS)
{
	auto reg3 = pci_read32(IPCIARGS, 0x8);
	return (uint8_t)(reg3 >> 24);
}

static inline uint8_t subClass(PCIARGS)
{
	auto reg3 = pci_read32(IPCIARGS, 0x8);
	return (uint8_t)(reg3 >> 16);
}

static inline uint8_t revisionId(PCIARGS)
{
	auto reg3 = pci_read8(IPCIARGS, 0x8);
	return reg3;
}

static inline uint8_t headerType(PCIARGS)
{
	auto reg = pci_read32(IPCIARGS, 0xc);
	return (uint8_t)(reg >> 16);
}

static inline uint8_t secondaryBus(PCIARGS)
{
	auto reg = pci_read16(IPCIARGS, 0x18);
	return reg >> 8;
}
