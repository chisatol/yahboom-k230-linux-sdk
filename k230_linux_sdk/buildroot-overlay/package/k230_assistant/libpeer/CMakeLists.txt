cmake_minimum_required(VERSION 3.5)
set(CMAKE_CXX_STANDARD 17)
project(libpeer_test)
set(CMAKE_SYSROOT /opt/toolchain/Xuantie-900-gcc-linux-6.6.0-glibc-x86_64-V2.10.1/sysroot)

# 设置编译器路径
set(CMAKE_C_COMPILER /opt/toolchain/Xuantie-900-gcc-linux-6.6.0-glibc-x86_64-V2.10.1/bin/riscv64-unknown-linux-gnu-gcc)

set(sdk_root "${PROJECT_SOURCE_DIR}/../../../../")
set(usr_root "${sdk_root}/output/k230_canmv_defconfig/host/riscv64-buildroot-linux-gnu/sysroot")

add_compile_definitions(LINUX_BUILD=1)

add_definitions("-DHTTP_DO_NOT_USE_CUSTOM_CONFIG -DMQTT_DO_NOT_USE_CUSTOM_CONFIG -DDISABLE_PEER_SIGNALING=true")

include_directories(./libpeer)
include_directories(./libpeer/include)
include_directories(./libpeer/third_party)
include_directories(./libpeer/third_party/cJSON)
include_directories(./libpeer/third_party/coreHTTP/source/include)
include_directories(./libpeer/third_party/coreHTTP/test/cbmc/include)
include_directories(./libpeer/third_party/coreHTTP/source/interface)
include_directories(./libpeer/third_party/coreMQTT/source/include)
include_directories(./libpeer/third_party/coreMQTT/test/cbmc/include)
include_directories(./libpeer/third_party/libsrtp/include)
include_directories(./libpeer/third_party/mbedtls/include)
include_directories(./libpeer/third_party/usrsctp/usrsctplib)

link_directories(${usr_root}/usr/lib/)
link_directories(${usr_root}/lib64/)

set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -L/opt/toolchain/Xuantie-900-gcc-linux-6.6.0-glibc-x86_64-V2.10.1/sysroot/usr/lib64/lp64d -L/opt/toolchain/Xuantie-900-gcc-linux-6.6.0-glibc-x86_64-V2.10.1/sysroot/")
add_subdirectory(libpeer)