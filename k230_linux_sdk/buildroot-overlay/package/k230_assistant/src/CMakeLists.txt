file(GLOB SRC_FILES "${CMAKE_CURRENT_SOURCE_DIR}/*.cc")
set(src ${SRC_FILES})
set(bin k230_assistant.elf)

include_directories(.)

include_directories(${usr_root}/usr/include/webrtc-audio-processing-1)
add_executable(${bin} ${src})
target_link_libraries(${bin} -Wl,--start-group sharpyuv webp png z mmz pthread alure2 asound libopenal.so ssl crypto curl cjson cjson_utils opus absl_raw_logging_internal absl_log_severity absl_bad_optional_access absl_string_view absl_throw_delegate webrtc-audio-processing-1 -Wl,--end-group)

target_link_libraries(${bin} peer cjson mbedtls mbedcrypto mbedx509 srtp2 usrsctp)
install(TARGETS ${bin} DESTINATION bin)
