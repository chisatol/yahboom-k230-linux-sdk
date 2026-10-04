set(CMAKE_C_COMPILER /opt/toolchain/Xuantie-900-gcc-linux-6.6.0-glibc-x86_64-V2.10.1/bin/riscv64-unknown-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER /opt/toolchain/Xuantie-900-gcc-linux-6.6.0-glibc-x86_64-V2.10.1/bin/riscv64-unknown-linux-gnu-g++)

add_library(everest
  library/everest.c
  library/x25519.c
  library/Hacl_Curve25519_joined.c)

target_include_directories(everest
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
         $<BUILD_INTERFACE:${MBEDTLS_DIR}/include>
         $<INSTALL_INTERFACE:include>
  PRIVATE include/everest
          include/everest/kremlib
          ${MBEDTLS_DIR}/library/)

if(INSTALL_MBEDTLS_HEADERS)

  install(DIRECTORY include/everest
    DESTINATION include
    FILE_PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ
    DIRECTORY_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
    FILES_MATCHING PATTERN "*.h")

endif(INSTALL_MBEDTLS_HEADERS)

install(TARGETS everest
  EXPORT MbedTLSTargets
  DESTINATION ${CMAKE_INSTALL_LIBDIR}
  PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ)
