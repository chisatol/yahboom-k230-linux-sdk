#!/bin/bash

# 设置环境变量
export PATH="/opt/toolchain/Xuantie-900-gcc-linux-6.6.0-glibc-x86_64-V2.10.1/bin/:${PATH}"
export CROSS_COMPILE="riscv64-unknown-linux-gnu-"

# 设置默认配置文件，如果命令行参数提供了配置文件，则使用提供的配置文件
conf="${1:-k230_canmv_01studio_defconfig}"

# 执行 make 命令
set -e  # 如果任何命令失败，脚本将立即退出
make "${conf}"
make
