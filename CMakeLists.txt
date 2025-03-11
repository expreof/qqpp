cmake_minimum_required(VERSION 3.26)

project(qqpp)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(qqpp INTERFACE)

target_include_directories(qqpp INTERFACE
$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
$<INSTALL_INTERFACE:include>)

include(FetchContent)

FetchContent_Declare(httplib URL https://github.com/yhirose/cpp-httplib/archive/refs/tags/v0.19.0.tar.gz)
FetchContent_Declare(json URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz)

FetchContent_MakeAvailable(httplib)
FetchContent_MakeAvailable(json)

target_link_libraries(qqpp INTERFACE httplib::httplib nlohmann_json::nlohmann_json)

add_subdirectory(example)