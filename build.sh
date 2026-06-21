#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# build.sh – Download Buildroot and build the Raspberry Pi 4 UVC gadget image
#
# Usage:  ./build.sh [clean]
#   clean  – wipe the output directory and start fresh
#
# The script obtains a proper FHS environment via buildFHSEnv (bubblewrap)
# so that Buildroot's configure scripts find /bin/true, /usr/bin/file, etc.
# No special wrapper is needed; just run:
#   ./build.sh
# ---------------------------------------------------------------------------
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BR_VERSION="2024.11.4"
BR_TARBALL="buildroot-${BR_VERSION}.tar.gz"
BR_URL="https://buildroot.org/downloads/${BR_TARBALL}"
BR_SHA256="8663961f6895b877c573729dc78d79ffdf5be27b5dbb5ab13251a8db2dc8fd9c"

BR_DIR="${SCRIPT_DIR}/buildroot-${BR_VERSION}"
BUILD_DIR="${SCRIPT_DIR}/output"
EXTERNAL_DIR="${SCRIPT_DIR}/buildroot"
DEFCONFIG="rpi4_uvc_gadget_defconfig"

# --- Clean ----------------------------------------------------------------
if [[ "${1:-}" == "clean" ]]; then
    echo "Cleaning output directory..."
    rm -rf "${BUILD_DIR}"
    exit 0
fi

# --- Build the FHS environment runner ------------------------------------
# buildFHSEnv produces a script at $result/bin/buildroot-env that enters a
# bubblewrap namespace with a standard FHS layout (needed by buildroot's
# hardcoded /bin/true, /usr/bin/file, etc.).
echo "Preparing FHS build environment..."
FHS_ENV="$(nix-build "${SCRIPT_DIR}/shell.nix" --no-out-link 2>/dev/null)"
FHS_RUN="${FHS_ENV}/bin/buildroot-env"
echo "FHS runner: ${FHS_RUN}"

# --- Download Buildroot ---------------------------------------------------
if [[ ! -d "${BR_DIR}" ]]; then
    if [[ ! -f "${BR_TARBALL}" ]]; then
        echo "Downloading Buildroot ${BR_VERSION}..."
        wget -q --show-progress "${BR_URL}" -O "${BR_TARBALL}"
    fi

    echo "Verifying checksum..."
    echo "${BR_SHA256}  ${BR_TARBALL}" | sha256sum -c -

    echo "Extracting Buildroot..."
    tar -xzf "${BR_TARBALL}"
    rm -f "${BR_TARBALL}"

    # NixOS fix: buildroot checks for the literal path /usr/bin/file (needed
    # by libtool) but NixOS puts 'file' in the nix store.  Inside the FHS
    # environment /usr/bin/file exists, but we also patch the check so the
    # error message makes sense if someone runs outside FHS.
    sed -i \
        's|check_prog_host "/usr/bin/file"|check_prog_host "file"|' \
        "${BR_DIR}/support/dependencies/dependencies.sh"
fi

# --- Configure -----------------------------------------------------------
mkdir -p "${BUILD_DIR}"

if [[ ! -f "${BUILD_DIR}/.config" ]]; then
    echo "Applying defconfig: ${DEFCONFIG}"
    "${FHS_RUN}" -c "
        make -C '${BR_DIR}' \
            O='${BUILD_DIR}' \
            BR2_EXTERNAL='${EXTERNAL_DIR}' \
            '${DEFCONFIG}'
    "
fi

# --- Build ---------------------------------------------------------------
NCPUS=$(nproc)
echo "Starting build with ${NCPUS} jobs (this will take a while)..."
"${FHS_RUN}" -c "
    make -C '${BR_DIR}' \
        O='${BUILD_DIR}' \
        BR2_EXTERNAL='${EXTERNAL_DIR}' \
        -j${NCPUS}
"

echo ""
echo "============================================================"
echo " Build complete!"
echo " SD-card image: ${BUILD_DIR}/images/sdcard.img"
echo ""
echo " Flash with:"
echo "   dd if=${BUILD_DIR}/images/sdcard.img of=/dev/sdX bs=4M status=progress"
echo "============================================================"
