# This file is part of the downloader library
#
# Copyright (c) 2018-2023, zero.kwok@foxmail.com
# For the full copyright and license information, please view the LICENSE
# file that was distributed with this source code.

cmake_policy(SET CMP0074 NEW)
cmake_minimum_required(VERSION 3.13)

project(downloader VERSION 0.2.3.0)

option(DOWNLOADER_BUILD_TOOLS "Enable to build the tools" ON)
option(DOWNLOADER_STATIC_RUNTIME "Enable link with runtime statically" OFF)
option(DOWNLOADER_BUILD_SHARED_LIB "Enable build shared libraries" OFF)

if(MSVC AND DOWNLOADER_STATIC_RUNTIME)
    foreach(flag_var CMAKE_CXX_FLAGS CMAKE_CXX_FLAGS_DEBUG CMAKE_CXX_FLAGS_RELEASE CMAKE_CXX_FLAGS_MINSIZEREL CMAKE_CXX_FLAGS_RELWITHDEBINFO)
        string(REGEX REPLACE "/MD" "/MT" ${flag_var} "${${flag_var}}")
    endforeach(flag_var)
endif()

if(MSVC)
    add_compile_options(/utf-8)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake;${CMAKE_MODULE_PATH}")

set(Boost_USE_STATIC_LIBS ON)
add_compile_definitions(BOOST_LIB_DIAGNOSTIC=1)
add_compile_definitions(AUTO_LINK_LIB_DIAGNOSTIC=1)

find_package(xcpr REQUIRED)
find_package(xlibcurl REQUIRED)
find_package(xzlib REQUIRED)
find_package(nlog REQUIRED)
find_package(utility REQUIRED)
find_package(Boost REQUIRED thread date_time filesystem)

include_directories(${utility_INCLUDE_DIRS})
link_directories   (${utility_LIBRARY_DIRS})
include_directories(${Boost_INCLUDE_DIRS})
link_directories   (${Boost_LIBRARY_DIRS})

set(INCLUDE_FILES 
    "include/uerror.h"
    "include/downloader.h")
set(SOURCE_FILES 
    "src/uerror.cpp"
    "src/downloader.cpp")

if(DOWNLOADER_BUILD_SHARED_LIB)
    add_library(${PROJECT_NAME} SHARED ${SOURCE_FILES} ${INCLUDE_FILES})
    target_compile_definitions(${PROJECT_NAME} PRIVATE DOWNLOADER_SHARE_LIB)
else()
    add_library(${PROJECT_NAME} STATIC ${SOURCE_FILES} ${INCLUDE_FILES})
    target_compile_definitions(${PROJECT_NAME} PUBLIC DOWNLOADER_STATIC_LIB)
endif()

set_target_properties(${PROJECT_NAME} PROPERTIES DEBUG_POSTFIX "d")
set_target_properties(${PROJECT_NAME} PROPERTIES 
    PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    COMPILE_PDB_NAME_DEBUG  "${PROJECT_NAME}d"
    COMPILE_PDB_NAME_RELEASE ${PROJECT_NAME}
    COMPILE_PDB_NAME_MINSIZEREL ${PROJECT_NAME}
    COMPILE_PDB_NAME_RELWITHDEBINFO ${PROJECT_NAME})
target_compile_definitions(${PROJECT_NAME} PRIVATE UTILITY_SUPPORT_BOOST)

target_include_directories(${PROJECT_NAME} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_PREFIX}/include>)
set_target_properties(${PROJECT_NAME} PROPERTIES PUBLIC_HEADER "${INCLUDE_FILES}")

target_link_libraries(${PROJECT_NAME} PUBLIC xcpr xlibcurl xzlib)
if(DOWNLOADER_STATIC_RUNTIME)
    target_link_libraries(${PROJECT_NAME} PUBLIC libnlog)
else()
    target_link_libraries(${PROJECT_NAME} PUBLIC nlog)
endif()

install(EXPORT ${PROJECT_NAME} FILE ${PROJECT_NAME}Config.cmake DESTINATION cmake)
install(TARGETS ${PROJECT_NAME}
    EXPORT ${PROJECT_NAME}
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib 
    RUNTIME DESTINATION bin
    PUBLIC_HEADER DESTINATION include)

get_target_property(PROJECT_PDB_NAME_DEBUG   ${PROJECT_NAME} COMPILE_PDB_NAME_DEBUG)
get_target_property(PROJECT_PDB_NAME_RELEASE ${PROJECT_NAME} COMPILE_PDB_NAME_RELEASE)
get_target_property(PROJECT_PDB_DIRECTORY    ${PROJECT_NAME} PDB_OUTPUT_DIRECTORY)
set(PROJECT_PDB_NAME "$<$<CONFIG:Debug>:${PROJECT_PDB_NAME_DEBUG}>$<$<NOT:$<CONFIG:Debug>>:${PROJECT_PDB_NAME_RELEASE}>.pdb")
install(FILES "${PROJECT_PDB_DIRECTORY}/${PROJECT_PDB_NAME}" DESTINATION lib OPTIONAL)
install(FILES "${PROJECT_PDB_DIRECTORY}/\${CMAKE_INSTALL_CONFIG_NAME}/${PROJECT_PDB_NAME}" DESTINATION lib  OPTIONAL)

install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/LICENSE DESTINATION ./ OPTIONAL)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/README.md DESTINATION ./ OPTIONAL)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tools DESTINATION ./)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/cmake DESTINATION ./)

if(DOWNLOADER_BUILD_TOOLS)
    add_subdirectory(tools)
endif()

if (NOT ${CMAKE_CURRENT_SOURCE_DIR} STREQUAL ${CMAKE_SOURCE_DIR})
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/build/templates.in" 
                   "${CMAKE_CURRENT_SOURCE_DIR}/build/build_static_static_x86_vc14.2.py")
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/build/templates.in" 
                   "${CMAKE_CURRENT_SOURCE_DIR}/build/build_static_dynamic_x86_vc14.2.py")
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/build/templates.in" 
                   "${CMAKE_CURRENT_SOURCE_DIR}/build/build_static_static_x86_vc14.1_xp.py")
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/build/templates.in" 
                   "${CMAKE_CURRENT_SOURCE_DIR}/build/build_dynamic_static_x86_vc14.1_xp.py")
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/build/templates.in" 
                   "${CMAKE_CURRENT_SOURCE_DIR}/build/build_dynamic_dynamic_x86_vc14.1_xp.py")
endif()