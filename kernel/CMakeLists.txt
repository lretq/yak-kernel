add_executable(kernel)

add_subdirectory(src)
add_subdirectory(io)
add_subdirectory(fs)

if (YAK_ARCH STREQUAL x86_64)
    add_subdirectory(arch/generic-limine)
endif()
add_subdirectory(arch/${YAK_ARCH})

yak_add_includes(
    include
    arch/${YAK_ARCH}/include
)

target_compile_options(kernel PRIVATE
	-fstack-protector-strong
	-fstack-check
	-mstack-protector-guard=global
)

target_compile_options(kernel PRIVATE
	$<$<CONFIG:DEBUG>:-fsanitize=undefined>
)

target_compile_options(kernel PRIVATE
     -fpie
     -fno-pic
     -mcmodel=kernel
     -Wall
     -Wextra
)

target_link_options(kernel
    PRIVATE -T${CMAKE_CURRENT_LIST_DIR}/arch/x86_64/linker.lds
    PRIVATE -static
)

target_compile_definitions(kernel 
    PUBLIC
	__KERNEL__
	${YAK_ARCH}
	ARCH_STR="${YAK_ARCH}"

	CONFIG_SYSCALL_LOG=1
	CONFIG_DEBUG=1
	CONFIG_UBSAN=1

	UACPI_NATIVE_ALLOC_ZEROED=1
	UACPI_SIZED_FREES=1
	FLANTERM_FB_DISABLE_BUMP_ALLOC=1
    PRIVATE
	__YAK_PRIVATE__
)

install(TARGETS kernel
    RUNTIME DESTINATION share/yak
)
