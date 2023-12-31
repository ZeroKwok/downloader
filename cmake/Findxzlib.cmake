# This file is part of the downloader projects.
# 
# Copyright (c) 2018-2023, zero.kwok@foxmail.com
# For the full copyright and license information, please view the LICENSE
# file that was distributed with this source code.

set(_PACKAGE_NAME    "zlib")
set(_PACKAGE_VERSION "${${_PACKAGE_NAME}_FIND_VERSION}")
set(_TARGET_NAME     "xzlib")

if(NOT TARGET ${_TARGET_NAME})
    include(platform)
    GetMSToolSet(ToolSet)
    GetRuntime(${CMAKE_CXX_FLAGS_DEBUG} DebugIsMT)

    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(ArchTag "x64")
    else()
        set(ArchTag "x86")
    endif()

    if (DebugIsMT)
        set(RuntimeTag "windows-static")
    else()
        set(RuntimeTag "windows")
    endif()
    set(PackageSuffix "${ArchTag}-${RuntimeTag}")

    set(matchlist "")
    foreach(item ${CMAKE_PREFIX_PATH})
        file(GLOB children RELATIVE ${item} ${item}/${PackageSuffix})
        foreach(child ${children})
            list(APPEND matchlist ${item}/${child})
        endforeach()
    endforeach()

    foreach(item ${matchlist})
        set(file "${item}/include/${_PACKAGE_NAME}.h")
        if(EXISTS "${file}")
            message("> Find Match: ${file}")
        
            add_library(${_TARGET_NAME} STATIC IMPORTED)
            set_target_properties(${_TARGET_NAME} PROPERTIES
                INTERFACE_INCLUDE_DIRECTORIES "${item}/include"
                IMPORTED_LINK_INTERFACE_LANGUAGES "C"
                IMPORTED_LOCATION_DEBUG "${item}/debug/lib/zlibd.lib"
                IMPORTED_LOCATION_RELWITHDEBINFO "${item}/lib/zlib.lib"
                )
            set(${_TARGET_NAME}_FOUND true)
            break()
        endif()
    endforeach()

    if (NOT ${_TARGET_NAME}_FOUND AND ${_TARGET_NAME}_FIND_REQUIRED)
        message("> Finding: ${_TARGET_NAME} ${_PACKAGE_VERSION}")
        message("> PackageSuffix: ${PackageSuffix}")
        message("> MatchList: ${matchlist}")
        message(FATAL_ERROR "${_TARGET_NAME} package not found.")
    endif()
endif()