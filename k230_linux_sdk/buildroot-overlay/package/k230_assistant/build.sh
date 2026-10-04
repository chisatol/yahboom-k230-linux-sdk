#!/bin/bash
rm -rf out
mkdir out

k230_bin_dir=`pwd`/k230_bin
rm -rf ${k230_bin_dir}
mkdir -p ${k230_bin_dir}


pushd out
cmake -DCMAKE_BUILD_TYPE=Release                \
-DCMAKE_INSTALL_PREFIX=`pwd`               \
-DCMAKE_C_COMPILER=/opt/toolchain/Xuantie-900-gcc-linux-6.6.0-glibc-x86_64-V2.10.1/bin/riscv64-unknown-linux-gnu-gcc \
-DCMAKE_CXX_COMPILER=/opt/toolchain/Xuantie-900-gcc-linux-6.6.0-glibc-x86_64-V2.10.1/bin/riscv64-unknown-linux-gnu-c++ \
..
make -j && make install
popd

cp out/bin/k230_assistant.elf ${k230_bin_dir}
cp config/config.json ${k230_bin_dir}
rm -rf out
