#!/bin/bash
set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
sdk=$(cd -- "$script_dir/../../../../../.." && pwd)
source_dir=${XIAOZHI_SOURCE:-"$sdk/dl/xiaozhi-assistant-source"}
websocket_dir=${XIAOZHI_WEBSOCKETPP:-"$sdk/dl/websocketpp-0.8.2"}
boost_dir=${XIAOZHI_BOOST:-/usr/include/boost}
conf=k230_canmv_yahboom_defconfig
output="$sdk/output/$conf"
build="$output/xiaozhi-build"
install_dir="$output/images/xiaozhi"
compiler="$output/host/bin/riscv64-unknown-linux-gnu-g++"

test -x "$compiler"
test -d "$source_dir/src/xiaozhi_client"
test -f "$websocket_dir/websocketpp/client.hpp"
test -f "$boost_dir/asio.hpp"
if [ "$(git -C "$source_dir" rev-parse HEAD)" != 912e88edb1825a635093545de59d0652f3a949fe ]; then
    echo "Unsupported Xiaozhi source revision" >&2
    exit 1
fi
if git -C "$source_dir" apply --reverse --check "$script_dir/client.patch"; then
    :
elif git -C "$source_dir" apply --check "$script_dir/client.patch"; then
    git -C "$source_dir" apply "$script_dir/client.patch"
else
    echo "Xiaozhi source does not match the supported patch" >&2
    exit 1
fi

mkdir -p "$build/include" "$install_dir"
if [ ! -e "$build/include/boost" ]; then
    ln -s "$boost_dir" "$build/include/boost"
fi
cd "$source_dir"
"$compiler" -std=c++17 -O2 -Wno-deprecated-declarations \
    -DBOOST_ERROR_CODE_HEADER_ONLY -I"$build/include" -I"$websocket_dir" \
    -Isrc/common -Isrc/xiaozhi_client/snd_core -I"$script_dir" \
    src/xiaozhi_client/*.cc src/xiaozhi_client/snd_core/acap.cc \
    src/xiaozhi_client/snd_core/capture_pcm.cc \
    src/xiaozhi_client/snd_core/play_pcm.cc \
    src/xiaozhi_client/snd_core/wake_word_generator.cc \
    src/xiaozhi_client/snd_core/webttsctrl.cc src/common/*.cc "$script_dir/mqtt_transport.cc" \
    -o "$install_dir/xiaozhi_client.new" \
    -lasound -lopus -lpaho-mqtt3as -lssl -lcrypto -lcurl -lz -lpthread
mv -f "$install_dir/xiaozhi_client.new" "$install_dir/xiaozhi_client"
ui_source="$source_dir/src/ui_and_ai/src/lv_100ask_xz_ai/src"
if [ ! -f "$ui_source/cJSON.c" ] || [ ! -f "$ui_source/cJSON.h" ]; then
    git -C "$source_dir" checkout HEAD -- \
        src/ui_and_ai/src/lv_100ask_xz_ai/src/cJSON.c \
        src/ui_and_ai/src/lv_100ask_xz_ai/src/cJSON.h
fi
sysroot="$output/host/riscv64-buildroot-linux-gnu/sysroot"
install -D -m 0755 "$sysroot/usr/lib/libpaho-mqtt3as.so.1.3.14" "$install_dir/lib/libpaho-mqtt3as.so.1"
"$output/host/bin/riscv64-unknown-linux-gnu-gcc" -O2 -DLV_CONF_INCLUDE_SIMPLE \
    -I"$sysroot/usr/include/lvgl" -I"$ui_source" \
    "$script_dir/xiaozhi-ui.c" "$ui_source/cJSON.c" \
    -o "$install_dir/xiaozhi-ui.new" -llvgl -lpthread -lm
mv -f "$install_dir/xiaozhi-ui.new" "$install_dir/xiaozhi-ui"
kws_dir="$sdk/buildroot-overlay/package/ai_demo/kws"
ai_dir="$source_dir/src/ui_and_ai/src"
"$compiler" -w -std=c++17 -O2 -I"$kws_dir" -I"$ai_dir" \
    -I"$sysroot/usr/include/ai_demo_common" -I"$sysroot/usr/include/opencv4" \
    -I"$sysroot/usr/include/libdrm" \
    "$script_dir/wake-word.cc" "$kws_dir/kws.cc" "$kws_dir/feature_pipeline.cc" \
    "$kws_dir/fft.cc" "$ai_dir/ai_base.cc" "$ai_dir/utils.cc" \
    -o "$install_dir/xiaozhi-wake.new" -Wl,--start-group \
    -lNncase.Runtime.Native -lnncase.rt_modules.k230 -lfunctional_k230 \
    -lopencv_imgcodecs -lopencv_imgproc -lopencv_core -lsharpyuv -l:libjpeg.so.9 \
    -lwebp -lpng -lz -lv4l2-drm -ldrm -ldisplay -lmmz -lpthread -Wl,--end-group
mv -f "$install_dir/xiaozhi-wake.new" "$install_dir/xiaozhi-wake"
install -D -m 0644 "$output/build/common/kmodel/ai_poc/kmodel/kws.kmodel" "$install_dir/kws.kmodel"
open_kws="$sdk/dl/k230-open-kws-source"
test -f "$open_kws/okws_bridge.cpp"
"$compiler" -std=c++17 -O2 -I"$open_kws" -I"$source_dir/src/common" \
    "$script_dir/open-wake.cc" "$open_kws"/okws_{bridge,decode,detect,encoder,enroll,frontend,model_nncase,npy}.cpp \
    -o "$install_dir/xiaozhi-open-wake.new" -lNncase.Runtime.Native \
    -lnncase.rt_modules.k230 -lfunctional_k230 -lmmz -lpthread
mv -f "$install_dir/xiaozhi-open-wake.new" "$install_dir/xiaozhi-open-wake"
install -D -m 0644 "$open_kws/kws.kmodel" "$install_dir/open-kws/kws.kmodel"
for asset in cmvn_mean.npy cmvn_std.npy mel_fb.npy window512.npy; do
    install -D -m 0644 "$open_kws/assets/$asset" "$install_dir/open-kws/assets/$asset"
done
"$compiler" -w -std=c++17 -O2 -I"$ai_dir" \
    -I"$sysroot/usr/include/ai_demo_common" -I"$sysroot/usr/include/opencv4" \
    -I"$sysroot/usr/include/libdrm" \
    "$script_dir/face-worker.cc" "$ai_dir/ai_base.cc" "$ai_dir/face_detection.cc" \
    "$ai_dir/face_recognition.cc" "$ai_dir/anchors_320.cc" "$ai_dir/anchors_640.cc" \
    "$ai_dir/sensor_buf_manager.cc" "$ai_dir/utils.cc" \
    -o "$install_dir/xiaozhi-face.new" -Wl,--start-group \
    -lNncase.Runtime.Native -lnncase.rt_modules.k230 -lfunctional_k230 \
    -lopencv_imgcodecs -lopencv_imgproc -lopencv_core -lsharpyuv -l:libjpeg.so.9 \
    -lwebp -lpng -lz -lv4l2-drm -ldrm -ldisplay -lmmz -lpthread -Wl,--end-group
mv -f "$install_dir/xiaozhi-face.new" "$install_dir/xiaozhi-face"
for model in face_detection_320 face_recognition; do
    install -D -m 0644 "$output/build/common/kmodel/ai_poc/kmodel/$model.kmodel" "$install_dir/$model.kmodel"
done
for asset in img_joke.png img_naughty.png img_think.png; do
    if [ ! -f "$source_dir/src/ui_and_ai/utils/xiaozhi/$asset" ]; then
        git -C "$source_dir" checkout HEAD -- "src/ui_and_ai/utils/xiaozhi/$asset"
    fi
    mkdir -p "$install_dir/assets"
    ffmpeg -v error -y -i "$source_dir/src/ui_and_ai/utils/xiaozhi/$asset" \
        -vf 'colorkey=0xffffff:0.12:0.08' -frames:v 1 -update 1 "$install_dir/assets/$asset.new.png"
    mv -f "$install_dir/assets/$asset.new.png" "$install_dir/assets/$asset"
done
echo "Built $install_dir/xiaozhi_client"
