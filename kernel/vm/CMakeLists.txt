yak_add_sources(
	pmm.cpp
	memblock.cpp
	map.cpp
	stack.cpp
	direct.cpp
)

if (CONFIG_USE_GENERIC_PAGEMAP)
	yak_add_sources(generic_pagemap.cpp)
endif()
