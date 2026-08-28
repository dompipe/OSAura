#!/usr/bin/env bash
set -euo pipefail

BUILD_DIR=${BUILD_DIR:-build}
IMAGE=${IMAGE:-${BUILD_DIR}/osaura.img}
EFI_BINARY=${EFI_BINARY:-${BUILD_DIR}/BOOTX64.EFI}
IMAGE_MIB=${IMAGE_MIB:-64}

for tool in mkfs.vfat mmd mcopy; do
    command -v "$tool" >/dev/null 2>&1 || {
        echo "missing required tool: $tool" >&2
        exit 1
    }
done

[ -f "$EFI_BINARY" ] || {
    echo "missing EFI binary: $EFI_BINARY" >&2
    exit 1
}

mkdir -p "$BUILD_DIR"
rm -f "$IMAGE"
dd if=/dev/zero of="$IMAGE" bs=1M count="$IMAGE_MIB" status=none
mkfs.vfat -F 32 -n OSAURA "$IMAGE" >/dev/null

mmd -i "$IMAGE" ::/EFI
mmd -i "$IMAGE" ::/EFI/BOOT
mmd -i "$IMAGE" ::/OSAURA
mcopy -i "$IMAGE" "$EFI_BINARY" ::/EFI/BOOT/BOOTX64.EFI

cat >"${BUILD_DIR}/osaura.cfg" <<'CFG'
name=OSAura
arch=x86_64
boot=uefi
shell=terminal
runtime=jx
CFG
mcopy -i "$IMAGE" "${BUILD_DIR}/osaura.cfg" ::/OSAURA/osaura.cfg

echo "created $IMAGE"
echo "UEFI fallback path: EFI/BOOT/BOOTX64.EFI"
