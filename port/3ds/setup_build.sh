#!/usr/bin/env bash
# ============================================================================
# Paper Mario 3DS - Build Environment Setup Script
# ============================================================================
# This script installs the devkitPro toolchain on Arch Linux/CachyOS and
# builds the 3DS homebrew port.
#
# Run: ./port/3ds/setup_build.sh
# ============================================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
PORT_DIR="${PROJECT_ROOT}/port/3ds"
BUILD_DIR="${PORT_DIR}/build"

echo "============================================"
echo " Paper Mario 3DS - Build Environment Setup"
echo "============================================"

# ---------------------------------------------------------------------------
# Step 1: Check for devkitPro
# ---------------------------------------------------------------------------
if [ -z "$DEVKITPRO" ] || [ -z "$DEVKITARM" ]; then
    echo ""
    echo "[!] devkitPro not detected."
    echo ""
    echo "To install devkitPro on Arch Linux:"
    echo "  1. Add the devkitPro pacman repository:"
    echo "     sudo wget https://pkg.devkitpro.org/devkitpro-key.tar.gz"
    echo "     sudo tar -xvf devkitpro-key.tar.gz -C /usr/share/pacman/keyrings/"
    echo ""
    echo "  2. Add to /etc/pacman.conf:"
    echo "     [dkp-libs]"
    echo "     Server = https://pkg.devkitpro.org/packages"
    echo ""
    echo "     [dkp-linux]"
    echo "     Server = https://pkg.devkitpro.org/packages/linux/\$arch/"
    echo ""
    echo "  3. Install toolchain:"
    echo "     sudo pacman -Syu devkitARM 3ds-dev 3ds-citro3d 3ds-tools"
    echo ""
    echo "  4. Set environment variables:"
    echo "     export DEVKITPRO=/opt/devkitpro"
    echo "     export DEVKITARM=\$DEVKITPRO/devkitARM"
    echo "     export PATH=\$DEVKITARM/bin:\$PATH"
    echo ""
    echo "  Or use the devkitPro installer:"
    echo "     wget https://apt.devkitpro.org/install-devkitpro-pacman"
    echo "     chmod +x ./install-devkitpro-pacman"
    echo "     sudo ./install-devkitpro-pacman"
    echo ""
    echo "Please install devkitPro and re-run this script."
    exit 1
fi

echo "[OK] devkitPro found:"
echo "     DEVKITPRO = $DEVKITPRO"
echo "     DEVKITARM = $DEVKITARM"

# ---------------------------------------------------------------------------
# Step 2: Verify tools
# ---------------------------------------------------------------------------
echo ""
echo "Checking tools..."

MISSING=0
for tool in arm-none-eabi-gcc arm-none-eabi-ld 3dsxtool; do
    if ! command -v "$tool" &> /dev/null; then
        echo "  [MISSING] $tool"
        MISSING=1
    else
        echo "  [OK] $tool"
    fi
done

if [ $MISSING -ne 0 ]; then
    echo ""
    echo "[!] Missing tools. Please install the 3ds-dev group:"
    echo "    sudo dkp-pacman -S 3ds-dev 3ds-citro3d 3ds-tools"
    exit 1
fi

# ---------------------------------------------------------------------------
# Step 3: Check for ROM and extract assets
# ---------------------------------------------------------------------------
echo ""
echo "Checking for ROM assets..."

ROM_PATH="${PROJECT_ROOT}/Extrackted ROM/Paper Mario (Europe) (En,Fr,De,Es).z64"
if [ -f "$ROM_PATH" ]; then
    echo "  [OK] ROM found: $ROM_PATH"
    
    # Extract assets if romfs doesn't exist
    if [ ! -d "${PORT_DIR}/assets" ]; then
        echo "  [INFO] Extracting assets..."
        mkdir -p "${PORT_DIR}/assets"
        python3 "${PORT_DIR}/tools/extract_assets.py" "$ROM_PATH" -o "${PORT_DIR}/assets" --skip-verify
    else
        echo "  [OK] Assets already extracted"
    fi
else
    echo "  [WARN] ROM not found at $ROM_PATH"
    echo "         Skipping asset extraction. The build will proceed without assets."
fi

# ---------------------------------------------------------------------------
# Step 4: Build
# ---------------------------------------------------------------------------
echo ""
echo "Building Paper Mario 3DS..."

cd "$PORT_DIR"
make clean || true
make

if [ $? -eq 0 ]; then
    echo ""
    echo "============================================"
    echo " Build SUCCESSFUL!"
    echo "============================================"
    echo ""
    echo "Output: ${BUILD_DIR}/3ds/PaperMario3DS.3dsx"
    echo ""
    echo "To run on Citra emulator:"
    echo "  make -C ${PORT_DIR} run"
    echo ""
    echo "To install on real 3DS:"
    echo "  1. Copy ${BUILD_DIR}/3ds/PaperMario3DS.3dsx to /3ds/ on your SD card"
    echo "  2. Launch via Homebrew Launcher"
    echo ""
else
    echo ""
    echo "[!] Build FAILED. Check errors above."
    exit 1
fi
