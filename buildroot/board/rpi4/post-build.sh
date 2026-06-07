#!/bin/sh
set -e

BOARD_DIR="$(dirname "$0")"

# Copy board-specific boot files to the binaries directory so
# genimage can find them alongside the kernel image.
cp -f "${BOARD_DIR}/config.txt"  "${BINARIES_DIR}/config.txt"
cp -f "${BOARD_DIR}/cmdline.txt" "${BINARIES_DIR}/cmdline.txt"

# Ensure init scripts are executable
chmod +x "${TARGET_DIR}/etc/init.d/S45usbgadget" 2>/dev/null || true
chmod +x "${TARGET_DIR}/etc/init.d/S99uvc-gadget" 2>/dev/null || true
