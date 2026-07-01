add_executable(yak)

include(CheckCXXCompilerFlag)
include(symbols.cmake)
include(build.cmake)

find_package(PkgConfig REQUIRED)

if (NOT YAK_HOSTED_MODE)
	yak_add_includes(${VENDOR_DIR}/freestnd-c-hdrs/${YAK_ARCH}/include)
	yak_add_includes(${VENDOR_DIR}/freestnd-cxx-hdrs/${YAK_ARCH}/include)
endif()

yak_add_includes(${VENDOR_DIR}/frigg/include)

find_package(Git QUIET)

add_custom_target(update_version_header ALL
	COMMAND ${CMAKE_COMMAND} 
	-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR} 
	-DBINARY_DIR=${CMAKE_BINARY_DIR} 
	-DVERSION_STR=${PROJECT_VERSION}
	-DVERSION_MAJOR=${PROJECT_VERSION_MAJOR}
	-DVERSION_MINOR=${PROJECT_VERSION_MINOR}
	-DVERSION_PATCH=${PROJECT_VERSION_PATCH}
	-P "${CMAKE_CURRENT_SOURCE_DIR}/get_git_hash.cmake"
	BYPRODUCTS "${CMAKE_BINARY_DIR}/generated/include/yak/version.h"
)

add_dependencies(yak update_version_header)

target_enable_fixdep(yak)

add_subdirectory(arch)
add_subdirectory(core)
add_subdirectory(io)
add_subdirectory(vm)
add_subdirectory(lib)

if(YAK_HOSTED_MODE)
	yak_add_includes(arch/hosted/${YAK_HOSTED_SYSTEM}/include)
else()
	yak_add_includes(arch/${YAK_ARCH}/include)
endif()

yak_add_includes(
	include
	arch

	# Contains version.h
	${CMAKE_BINARY_DIR}/generated/include
	# Contains config.h
	${CMAKE_BINARY_DIR}/kconfig/include/generated
)

if (YAK_HOSTED_MODE) 
yak_defines(
	YAK_HOSTED_MODE=1
	YAK_HOSTED_SYSTEM=${YAK_HOSTED_SYSTEM}
)
endif()

yak_defines(
	__KERNEL__
	ARCH_STR="${YAK_ARCH}"
	${YAK_ARCH}
)

if (NOT CONFIG_STACK_CANARIES)
	yak_compile_options(-fno-stack-protector)
elseif(NOT YAK_HOSTED_MODE)
	yak_compile_options(
	-fstack-protector-strong
	-fstack-check
	-mstack-protector-guard=global
	)
endif()

if (CONFIG_LTO)
	target_enable_lto(yak)
endif()

if (CONFIG_UBSAN)
	yak_compile_options(
		-fsanitize=undefined
	)
endif()

if (CONFIG_BUILD_TYPE_DEBUG)
	yak_compile_options(-O0)
elseif (CONFIG_BUILD_TYPE_DEBUG_OPTIMIZED)
	check_cxx_compiler_flag("-fextend-variable-liveness" HAS_EXTEND_VARIABLE_LIVENESS)
	if (HAS_EXTEND_VARIABLE_LIVENESS)
		yak_compile_options(-Og -fextend-variable-liveness)
	else()
		yak_compile_options(-O0)
	endif()
elseif(CONFIG_BUILD_TYPE_RELEASE)
	yak_compile_options(-O2)
elseif(CONFIG_BUILD_TYPE_MINSIZEREL)
	yak_compile_options(-Os)
elseif(CONFIG_BUILD_TYPE_FASTFLAGSREL)
	yak_compile_options(-O3)
endif()

yak_compile_options(
	-ffile-prefix-map=${PROJECT_ROOT_NORMALIZED}=./
	-fmacro-prefix-map=${PROJECT_ROOT_NORMALIZED}=./
)

if (NOT YAK_HOSTED_MODE)
	yak_compile_options(
	-ffreestanding
	-nostdinc
	-fno-pie
	-fno-pic
	)
endif()

yak_compile_options(
	-ggdb3
	-fno-omit-frame-pointer
	-fno-strict-aliasing
	-fno-semantic-interposition
	-fvisibility=hidden
	-fasynchronous-unwind-tables
	-pipe
	-fdata-sections
	-ffunction-sections
	-Wall
	-Wextra
	-Wshadow
	-Wimplicit-fallthrough
	#-Wconversion
	-Wnull-dereference
	$<$<CXX_COMPILER_ID:Clang>:-Wthread-safety>
	$<$<COMPILE_LANGUAGE:CXX>:-fno-threadsafe-statics>
	$<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>
	$<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>
	$<$<COMPILE_LANGUAGE:CXX>:-fcheck-new>
	$<$<COMPILE_LANGUAGE:CXX>:-fno-use-cxa-atexit>
	$<$<CXX_COMPILER_ID:Clang>:-fstrict-vtable-pointers>
)

if (NOT YAK_HOSTED_MODE)
yak_link_options(
	-nostdlib
)
endif()

yak_link_options(
	--eh-frame-hdr
	-gc-sections
)

yak_add_sources(
	${VENDOR_DIR}/flanterm/src/flanterm.c
	${VENDOR_DIR}/flanterm/src/flanterm_backends/fb.c
)

yak_add_includes(${VENDOR_DIR}/flanterm/src)

install(TARGETS yak DESTINATION share/yak)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/yak.sym DESTINATION share/yak)
