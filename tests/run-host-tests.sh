#!/bin/bash
set -euo pipefail
mocha_tests="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
mocha_root="$(cd "$mocha_tests/../../../.." && pwd)"
mocha_out="${1:-$(mktemp -d -t mocha-host-tests.XXXXXX)}"
mkdir -p "$mocha_out"
echo "Host test output: $mocha_out"

mocha_flags=(
    -m32 -g -O1 -fno-omit-frame-pointer -fsanitize=address,undefined -fno-pie
    '-D__unused=__attribute__((unused))'
    -I"$mocha_root/hardware/libhardware/include"
    -I"$mocha_root/system/core/libcutils/include"
    -I"$mocha_root/system/core/liblog/include"
    -I"$mocha_root/system/core/libsystem/include"
    -I"$mocha_root/system/media/camera/include"
)
mocha_link=(
    -m32 -fsanitize=address,undefined -no-pie -pthread -ldl
    -L"$mocha_root/out/host/linux-x86/lib"
    -Wl,-rpath,"$mocha_root/out/host/linux-x86/lib" -llog
)
export ASAN_OPTIONS=detect_leaks=1:halt_on_error=1
export UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1

g++ -std=gnu++11 "${mocha_flags[@]}" -I"$mocha_tests/../sensors" \
    "$mocha_tests/sensor_queue_test.cpp" "${mocha_link[@]}" -o "$mocha_out/sensor-queue-test"
timeout 40s "$mocha_out/sensor-queue-test"

gcc -std=gnu11 -include stddef.h "${mocha_flags[@]}" -I"$mocha_root/system/media/private/camera/include" \
    -c "$mocha_root/system/media/camera/src/camera_metadata.c" -o "$mocha_out/camera_metadata.o"
g++ -std=gnu++11 "${mocha_flags[@]}" -I"$mocha_tests/host-include" \
    "$mocha_tests/camera_lifetime_test.cpp" "$mocha_out/camera_metadata.o" \
    "${mocha_link[@]}" -o "$mocha_out/camera-lifetime-test"
timeout 30s "$mocha_out/camera-lifetime-test"
