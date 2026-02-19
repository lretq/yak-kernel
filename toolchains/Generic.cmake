macro(require_env ENV_NAME)
	if(DEFINED CACHE{${ENV_NAME}})
		message(STATUS "${ENV_NAME} from cache: ${${ENV_NAME}}")

	elseif(DEFINED ENV{${ENV_NAME}})
		set(${ENV_NAME} $ENV{${ENV_NAME}} CACHE STRING "${ENV_NAME} env cache")
		message(STATUS "${ENV_NAME} from env: ${${ENV_NAME}}")

	else()
		message(FATAL_ERROR "'${ENV_NAME}' is not set in cache or environment!")
	endif()
endmacro()

require_env(YAK_ARCH)
require_env(YAK_TOOLCHAIN)
require_env(YAK_TRIPLET)

set(CMAKE_SYSTEM_NAME "Yak")
set(CMAKE_SYSTEM_PROCESSOR ${YAK_ARCH})

add_compile_options(
	-ffreestanding
	-nostdinc
	-fno-omit-frame-pointer
	-fno-strict-aliasing
	-mgeneral-regs-only
	-pipe
	-fdata-sections
	-ffunction-sections
	$<$<COMPILE_LANGUAGE:CXX>:-fno-threadsafe-statics>
	$<$<CXX_COMPILER_ID:Clang>:-fstrict-vtable-pointers>
	$<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>
	$<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>
	$<$<COMPILE_LANGUAGE:CXX>:-fcheck-new>
	# $<$<COMPILE_LANGUAGE:CXX>:-fsized-deallocation>
)

add_link_options(
	-nostdlib
	-Wl,-zmax-page-size=0x1000
	-Wl,--gc-sections
)

if(YAK_ARCH STREQUAL "x86_64")
	add_compile_options(
		-mno-red-zone
		-mno-mmx -mno-sse -mno-sse2
		-mno-80387
	)
else()
	message(FATAL_ERROR "Unsupported YAK_ARCH: ${YAK_ARCH}")
endif()

if(YAK_TOOLCHAIN STREQUAL "llvm")
	set(CMAKE_C_COMPILER clang)
	set(CMAKE_CXX_COMPILER clang++)
	set(CMAKE_ASM_COMPILER clang)
	add_compile_options(--target=${YAK_TRIPLET})
	add_link_options(
		--target=${YAK_TRIPLET}
		-fuse-ld=lld
	)
elseif(YAK_TOOLCHAIN STREQUAL "gcc")
	set(CMAKE_C_COMPILER ${YAK_TRIPLET}-gcc)
	set(CMAKE_CXX_COMPILER ${YAK_TRIPLET}-g++)
	set(CMAKE_ASM_COMPILER ${YAK_TRIPLET}-gcc)
else()
	message(FATAL_ERROR "Unsupported YAK_TOOLCHAIN: ${YAK_TOOLCHAIN}")
endif()

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# Root path handling
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
