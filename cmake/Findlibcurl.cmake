# This file is part of the MBackupper projects.
# 
# Copyright (c) 2018-2023, MBackupper Teams
# For the full copyright and license information, please view the LICENSE
# file that was distributed with this source code.
# 
# Author:  GuoJH
# Contact: zero.kwok@foxmail.com

set(_PACKAGE_NAME    "curl")
set(_PACKAGE_VERSION "${${_PACKAGE_NAME}_FIND_VERSION}")

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
    set(file "${item}/include/${_PACKAGE_NAME}")
    if(EXISTS "${file}")
        message("> Find Match: ${file}")
        
        add_library(libcurl STATIC IMPORTED)
        set_target_properties(libcurl PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${item}/include"
            INTERFACE_LINK_DIRECTORIES "${item}/lib"
            INTERFACE_LINK_LIBRARIES "wldap32;winmm;ws2_32;advapi32;crypt32;zlib"
            IMPORTED_LINK_INTERFACE_LANGUAGES "CXX"
            IMPORTED_LOCATION_DEBUG "${item}/debug/lib/libcurl-d.lib"
            IMPORTED_LOCATION_RELWITHDEBINFO "${item}/lib/libcurl.lib"
            )
        set(${_PACKAGE_NAME}_FOUND true)
        break()
    endif()
endforeach()

if (NOT ${_PACKAGE_NAME}_FOUND AND ${_PACKAGE_NAME}_FIND_REQUIRED)
    message("> Finding: ${_PACKAGE_NAME} ${_PACKAGE_VERSION}")
    message("> PackageSuffix: ${PackageSuffix}")
    message("> MatchList: ${matchlist}")
    message(FATAL_ERROR "${_PACKAGE_NAME} package not found.")
endif()
