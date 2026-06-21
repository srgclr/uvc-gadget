#!/bin/sh
set -e

BOARD_DIR="$(dirname "$0")"

# Copy board-specific boot files to the binaries directory so
# genimage can find them alongside the kernel image.
cp -f "${BOARD_DIR}/config.txt"  "${BINARIES_DIR}/config.txt"
cp -f "${BOARD_DIR}/cmdline.txt" "${BINARIES_DIR}/cmdline.txt"

# rpi-firmware installs into BINARIES_DIR/rpi-firmware/; flatten the files
# that genimage.cfg expects at the top level of BINARIES_DIR.
RPI_FW="${BINARIES_DIR}/rpi-firmware"
cp -f "${RPI_FW}/bcm2711-rpi-4-b.dtb" "${BINARIES_DIR}/"
cp -f "${RPI_FW}/fixup4.dat"           "${BINARIES_DIR}/"
cp -f "${RPI_FW}/start4.elf"           "${BINARIES_DIR}/"
mkdir -p "${BINARIES_DIR}/overlays"
cp -f "${RPI_FW}/overlays/dwc2.dtbo"        "${BINARIES_DIR}/overlays/"
cp -f "${RPI_FW}/overlays/disable-bt.dtbo"  "${BINARIES_DIR}/overlays/"

# Remove services not needed on a dedicated camera appliance
rm -f "${TARGET_DIR}/etc/init.d/S01syslogd"
rm -f "${TARGET_DIR}/etc/init.d/S02klogd"
rm -f "${TARGET_DIR}/etc/init.d/S50crond"

# Ensure init scripts are executable
chmod +x "${TARGET_DIR}/etc/init.d/S45usbgadget" 2>/dev/null || true
chmod +x "${TARGET_DIR}/etc/init.d/S99uvc-gadget" 2>/dev/null || true
