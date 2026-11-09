#!/bin/sh
# Build ppc-replay: replays a PPCGPU_RECORD recording through the Vulkan
# backend with no guest (needs a built qemu, shaderc and SPIRV-Cross).
#   tools/build-replay.sh [output]        default: qemu/build/ppc-replay
set -eu
repo=$(cd "$(dirname "$0")/.." && pwd)
q=$repo/qemu
out=${1:-$q/build/ppc-replay}
lib=$q/build/hw/display/ppc_mac_gpu_vulkan_target/release/libppc_mac_gpu_vulkan.a
[ -f "$lib" ] || { echo "build qemu first: $lib is missing" >&2; exit 1; }
d=$q/hw/display
cc -O1 -g -w -I"$d" -I"$d/r300" -o "$out" "$repo/tools/ppc-replay.c" \
    "$d/r300/r300_draw.c" "$d/r300/r300_state.c" "$d/r300/r300_us.c" \
    "$d/r300/r300_pvs.c" "$d/r300/r300_spirv.c" "$lib" \
    $(pkg-config --cflags --libs shaderc spirv-cross-c-shared) -lvulkan -ldl -lm -lpthread
echo "built $out"
