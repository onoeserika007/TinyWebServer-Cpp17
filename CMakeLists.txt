cmake_minimum_required(VERSION 3.22)

# Set compilers before project()
set(CMAKE_C_COMPILER /usr/bin/gcc-13)
set(CMAKE_CXX_COMPILER /usr/bin/g++-13)

project(webserver)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_BUILD_TYPE "Debug")
#set(CMAKE_BUILD_TYPE "Release")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g")

# Set project root directory
set(PROJECT_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR})
add_definitions(-DPROJECT_ROOT_DIR="${PROJECT_ROOT_DIR}")

message(STATUS "C++ compiler: ${CMAKE_CXX_COMPILER}")
message(STATUS "C compiler: ${CMAKE_C_COMPILER}")
message(STATUS "C++ compiler version: ${CMAKE_CXX_COMPILER_VERSION}")

set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)
set(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/lib)

# 设置项目库文件搜索路径 -L
link_directories(${PROJECT_SOURCE_DIR}/lib)

# third party
add_subdirectory(${PROJECT_SOURCE_DIR}/thirdparty/jsoncpp)

# core
add_subdirectory(${PROJECT_SOURCE_DIR}/src/base)
add_subdirectory(${PROJECT_SOURCE_DIR}/src/webserver)
add_subdirectory(${PROJECT_SOURCE_DIR}/src/util)

# test
add_subdirectory(${PROJECT_SOURCE_DIR}/test/minimal_plain)
add_subdirectory(${PROJECT_SOURCE_DIR}/test/webbench)
add_subdirectory(${PROJECT_SOURCE_DIR}/test/minimal_epoll)
add_subdirectory(${PROJECT_SOURCE_DIR}/test/log_test)
add_subdirectory(${PROJECT_SOURCE_DIR}/test/config_test)
add_subdirectory(${PROJECT_SOURCE_DIR}/test/mysql_pool_test)