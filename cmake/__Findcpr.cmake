# This file is part of the MBackupper projects.
# 
# Copyright (c) 2018-2023, zero.kwok@foxmail.com
# For the full copyright and license information, please view the LICENSE
# file that was distributed with this source code.

set(_PACKAGE_NAME    "cpr")
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
    set(file "${item}/share/${_PACKAGE_NAME}/${_PACKAGE_NAME}Config.cmake")
    if(EXISTS "${file}")
        message("> Find Match: ${file}")
        include(${file})
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
