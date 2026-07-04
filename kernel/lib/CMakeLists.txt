if (NOT YAK_HOSTED_MODE)
yak_add_sources(
	string.c
	stack.cpp
)
endif()

yak_add_sources(
	assert.cpp
	abort.cpp
	frigg.cpp
)

if (CONFIG_UBSAN)
	yak_add_sources(ubsan.cpp)
endif()
