cmake_minimum_required(VERSION 3.26)

project(qqpp)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# 引入json解析库
# FetchContent_Declare(json URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz)
# 引入http请求库
# FetchContent_Declare(cpr GIT_REPOSITORY https://github.com/libcpr/cpr.git GIT_TAG 1.11.0)
# FetchContent_Declare(tabulate GIT_REPOSITORY https://github.com/p-ranav/tabulate.git GIT_TAG v1.5)
FetchContent_Declare(httplib GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git GIT_TAG v0.18.3)

FetchContent_MakeAvailable(httplib)

message("http source dir: " ${httplib_SOURCE_DIR})
message("httplib install dir: " ${CMAKE_INSTALL_LIBDIR})

add_executable(qqpp)
target_sources(qqpp PRIVATE main.cpp)

# target_include_directories(qqpp PRIVATE ${PROJECT_SOURCE_DIR}/include)

target_link_libraries(qqpp PRIVATE httplib::httplib)