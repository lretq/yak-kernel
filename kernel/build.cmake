function(yak_add_sources)
	foreach(arg ${ARGV})
		if(IS_DIRECTORY ${arg})
			message(FATAL_ERROR "yak_add_sources() called on directory")
		endif()
		target_sources(yak PRIVATE ${arg})
	endforeach()
endfunction()

function(yak_add_includes)
	cmake_parse_arguments(PARSE_ARGV 0 "YAK" "SYSTEM" "" "")

    if(YAK_SYSTEM)
        # If SYSTEM was passed, forward it to target_include_directories
        target_include_directories(yak SYSTEM PRIVATE ${YAK_UNPARSED_ARGUMENTS})
    else()
        # Otherwise, perform the standard private include
        target_include_directories(yak PRIVATE ${YAK_UNPARSED_ARGUMENTS})
    endif()
endfunction()

function(yak_compile_options)
	target_compile_options(yak PRIVATE ${ARGV})
endfunction()

function(yak_link_options)
	foreach(arg ${ARGV})
		if (YAK_HOSTED_MODE)
		target_link_options(yak PRIVATE "LINKER:${arg}")
		else()
		target_link_options(yak PRIVATE ${arg})
		endif()
	endforeach()
endfunction()

function(yak_link_depends)
	set_target_properties(yak PROPERTIES LINK_DEPENDS ${ARGV})
endfunction()

function(yak_defines)
	target_compile_definitions(yak PRIVATE ${ARGN})
endfunction()

function(yak_link)
	target_link_libraries(yak PRIVATE ${ARGN})
endfunction()

function(target_enable_fixdep target)
	set_target_properties(${target} PROPERTIES C_COMPILER_LAUNCHER
    		"python3;${CMAKE_SOURCE_DIR}/scripts/fixdeps.py;${KCONFIG_SYNC_DIR}")
	set_target_properties(${target} PROPERTIES CXX_COMPILER_LAUNCHER
    		"python3;${CMAKE_SOURCE_DIR}/scripts/fixdeps.py;${KCONFIG_SYNC_DIR}")
endfunction()

function(target_enable_lto target)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT supported)

    if(NOT supported)
        return()
    endif()

    set_property(TARGET ${target} PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)

	#if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
		#message("Enable ThinLTO for target ${target}")
		#target_compile_options(${target} PRIVATE -flto=thin)
		#target_link_options(${target} PRIVATE -flto=thin)
	#endif()
endfunction()
