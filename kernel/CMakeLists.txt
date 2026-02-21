add_executable(kernel)

add_custom_command(TARGET kernel POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} --only-keep-debug $<TARGET_FILE:kernel> $<TARGET_FILE:kernel>.debug
    COMMAND ${CMAKE_STRIP} --strip-debug --strip-unneeded $<TARGET_FILE:kernel>
    COMMAND ${CMAKE_OBJCOPY} --add-gnu-debuglink=$<TARGET_FILE:kernel>.debug $<TARGET_FILE:kernel>
    COMMENT "Generating debug file for kernel"
)

add_custom_command(TARGET kernel POST_BUILD
    COMMAND ${CMAKE_NM} $<TARGET_FILE:kernel>.debug > $<TARGET_FILE:kernel>.sym
    COMMENT "Generating symbols file for kernel"
)

option(KERNEL_LTO "enable lto" OFF)

if (KERNEL_LTO)
    set_property(TARGET kernel PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

add_subdirectory(src)
add_subdirectory(src-cpp)
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
    #-finstrument-functions
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

	CONFIG_PROFILER=0
	CONFIG_LAZY_IPL=1
	CONFIG_SERIAL=1
	CONFIG_SYSCALL_LOG=1
	CONFIG_DEBUG=1
	CONFIG_UBSAN=1

	UACPI_NATIVE_ALLOC_ZEROED=1
	FLANTERM_FB_DISABLE_BUMP_ALLOC=1
    PRIVATE
	__YAK_PRIVATE__
)

install(TARGETS kernel
    RUNTIME DESTINATION share/yak
)

install(FILES ${CMAKE_BINARY_DIR}/kernel/kernel.sym
    DESTINATION share/yak
)
