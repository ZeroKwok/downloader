cmake_policy(SET CMP0074 NEW)
cmake_minimum_required(VERSION 3.13)

project(example_for_downloader)

foreach(flag_var CMAKE_CXX_FLAGS CMAKE_CXX_FLAGS_DEBUG CMAKE_CXX_FLAGS_RELEASE CMAKE_CXX_FLAGS_MINSIZEREL CMAKE_CXX_FLAGS_RELWITHDEBINFO)
    string(REGEX REPLACE "/MD" "/MT" ${flag_var} "${${flag_var}}")
endforeach(flag_var)

add_executable(${PROJECT_NAME} "example.cpp")
target_compile_definitions(${PROJECT_NAME} PRIVATE UTILITY_SUPPORT_BOOST)
target_link_libraries(${PROJECT_NAME} PRIVATE downloader)