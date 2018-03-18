cmake_minimum_required(VERSION 3.6)

project(data-aware-zsim)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -pthread")

file(GLOB_RECURSE SOURCE_FILES "src/*.cpp" "src/*.hpp" "src/*.h")

include_directories(/home/farid/pin/source/include/pin/)
include_directories(/home/farid/pin/source/include/pin/gen)
include_directories(/home/farid/pin/source/include/launcher)

add_executable(data-aware-zsim ${SOURCE_FILES} ${SOURCE_FILES_2})


