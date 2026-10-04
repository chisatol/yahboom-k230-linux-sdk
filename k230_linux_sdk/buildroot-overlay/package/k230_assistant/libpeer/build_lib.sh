rm -rf out
mkdir out

pushd out
cmake -DCMAKE_BUILD_TYPE=Release                \
-DCMAKE_INSTALL_PREFIX=`pwd`               \
-DCMAKE_C_COMPILER=/opt/toolchain/Xuantie-900-gcc-linux-6.6.0-glibc-x86_64-V2.10.1/bin/riscv64-unknown-linux-gnu-gcc \
..
make -j && make install
popd

rm -rf ../dist
mkdir ../dist
mkdir ../dist/include
mkdir ../dist/lib

cp -r out/dist/* ../dist
cp out/lib/libpeer.a ../dist/lib
cp -r libpeer/include ../dist/include/peer

rm -rf out