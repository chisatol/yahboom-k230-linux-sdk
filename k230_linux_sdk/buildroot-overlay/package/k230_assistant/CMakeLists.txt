cmake_minimum_required(VERSION 3.5)
set(CMAKE_CXX_STANDARD 17)
project(k230_assistant)

# read .last_conf
file(READ "../../../.last_conf" LAST_CONF_CONTENT)
# match board
string(REGEX MATCH "CONF=([^\n]+)" _ "${LAST_CONF_CONTENT}")
if (CMAKE_MATCH_1)
    set(CONF_VALUE "${CMAKE_MATCH_1}")
    message(STATUS "Current CONF: ${CONF_VALUE}")
else()
    message(FATAL_ERROR "not found CONF in .last_conf")
endif()

# 假设 Buildroot 配置文件路径为 $BR2_CONFIG
set(BR2_CONFIG "../../configs/${CONF_VALUE}")

set(CMAKE_C_COMPILER /opt/toolchain/Xuantie-900-gcc-linux-6.6.0-glibc-x86_64-V2.10.1/bin/riscv64-unknown-linux-gnu-gcc)

set(sdk_root "${PROJECT_SOURCE_DIR}/../../../")
set(usr_root "${sdk_root}/output/${CONF_VALUE}/host/riscv64-buildroot-linux-gnu/sysroot")

include_directories(${usr_root}/usr/include)
include_directories(./dist/include)
include_directories(./dist/include/peer)

link_directories(${usr_root}/usr/lib/)
link_directories(./dist/lib)

set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -L/opt/toolchain/Xuantie-900-gcc-linux-6.6.0-glibc-x86_64-V2.10.1/sysroot/usr/lib64/lp64d -L/opt/toolchain/Xuantie-900-gcc-linux-6.6.0-glibc-x86_64-V2.10.1/sysroot/")

add_subdirectory(src)

