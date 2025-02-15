cmake_minimum_required(VERSION 3.26)

project(qqpp)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

FetchContent_Declare(httplib URL https://github.com/yhirose/cpp-httplib/archive/refs/tags/v0.19.0.tar.gz)
FetchContent_Declare(json URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz)

FetchContent_MakeAvailable(httplib)
FetchContent_MakeAvailable(json)

add_executable(qqpp)
target_sources(qqpp PRIVATE main.cpp)

target_include_directories(qqpp PRIVATE ${PROJECT_SOURCE_DIR}/include)

target_link_libraries(qqpp PRIVATE httplib::httplib nlohmann_json::nlohmann_json)