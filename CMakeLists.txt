cmake_minimum_required(VERSION 3.6)

project(data-aware-zsim)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -std=c++0x -Wall -Wno-unknown-pragmas -fomit-frame-pointer -fno-stack-protector -MMD -DBIGARRAY_MULTIPLIER=1 -DUSING_XED -DTARGET_IA32E -DHOST_IA32E -fPIC -DTARGET_LINUX -Wl,--hash-style=sysv -Wl,-Bsymbolic -Wl")

file(GLOB_RECURSE SOURCE_FILES "src/*.cpp" "src/*.hpp" "src/*.h")

set(SOURCE_FILES_2 /home/farid/pin/source/include/pin/gen/types_foundation.TLH)

include_directories(/home/farid/pin/source/include/pin/)
include_directories(/home/farid/pin/source/include/pin/gen)
include_directories(/home/farid/pin/source/include/launcher)

add_executable(data-aware-zsim ${SOURCE_FILES} ${SOURCE_FILES_2})


