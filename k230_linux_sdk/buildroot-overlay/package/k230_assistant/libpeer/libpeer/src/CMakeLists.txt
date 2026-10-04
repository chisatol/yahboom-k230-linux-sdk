project(libpeer)

set(CMAKE_C_COMPILER /opt/toolchain/Xuantie-900-gcc-linux-6.6.0-glibc-x86_64-V2.10.1/bin/riscv64-unknown-linux-gnu-gcc)
file(GLOB SRCS "*.c")

add_library(peer
  ${SRCS}
)

include_directories(peer PUBLIC
  ${HTTP_INCLUDE_PUBLIC_DIRS}
  ${MQTT_INCLUDE_PUBLIC_DIRS}
)

add_dependencies(peer cjson mbedtls usrsctp srtp2)
target_link_libraries(peer ${DEP_LIBS})
target_link_libraries(peer pthread)
message(${CMAKE_BINARY_DIR})
install(TARGETS peer LIBRARY DESTINATION ${CMAKE_INSTALL_FULL_LIBDIR})
