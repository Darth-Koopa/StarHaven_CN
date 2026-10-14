#!/usr/bin/env bash
# ============================================================================
# Paper Mario 3DS - Docker Build Script
# ============================================================================
# This script builds the 3DS port inside a Docker container.
# No local devkitPro installation required!
#
# Requirements:
#   - Docker (or Podman)
#
# Usage:
#   ./port/3ds/docker_build.sh
# ============================================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
IMAGE_NAME="papermario-3ds-builder"

echo "============================================"
echo " Paper Mario 3DS - Docker Build"
echo "============================================"

# Check for Docker
if ! command -v docker &> /dev/null; then
    echo "[!] Docker not found. Trying Podman..."
    if command -v podman &> /dev/null; then
        DOCKER_CMD="podman"
    else
        echo "[!] Neither Docker nor Podman found."
        echo "    Please install Docker: https://docs.docker.com/get-docker/"
        exit 1
    fi
else
    DOCKER_CMD="docker"
fi

echo "[OK] Using container engine: $DOCKER_CMD"

# Build the toolchain image if it doesn't exist
echo ""
echo "[BUILD] Checking for toolchain image..."
if ! $DOCKER_CMD image inspect "$IMAGE_NAME" &> /dev/null; then
    echo "[BUILD] Toolchain image not found. Building..."
    echo "[BUILD] This will download the devkitPro base image (~2GB)"
    echo "[BUILD] and install 3DS development tools. This may take 10-15 minutes."
    echo ""
    $DOCKER_CMD build -t "$IMAGE_NAME" -f "$SCRIPT_DIR/Dockerfile" "$PROJECT_ROOT"
else
    echo "[OK] Toolchain image found: $IMAGE_NAME"
fi

# Build the project
echo ""
echo "[BUILD] Compiling Paper Mario 3DS..."
$DOCKER_CMD run --rm \
    -v "$PROJECT_ROOT:/project" \
    -e DEVKITPRO=/opt/devkitpro \
    -e DEVKITARM=/opt/devkitpro/devkitARM \
    "$IMAGE_NAME" \
    make -C /project/port/3ds

# Check result
if [ -f "$PROJECT_ROOT/port/3ds/build/3ds/PaperMario3DS.3dsx" ]; then
    echo ""
    echo "============================================"
    echo " Build SUCCESSFUL!"
    echo "============================================"
    echo ""
    echo "Output: port/3ds/build/3ds/PaperMario3DS.3dsx"
    echo "Size:   $(ls -lh "$PROJECT_ROOT/port/3ds/build/3ds/PaperMario3DS.3dsx" | awk '{print $5}')"
    echo ""
    echo "To run on Citra:"
    echo "  $DOCKER_CMD run --rm -v \"$PROJECT_ROOT:/project\" \"$IMAGE_NAME\" citra /project/port/3ds/build/3ds/PaperMario3DS.3dsx"
    echo ""
    echo "To copy to SD card for real hardware:"
    echo "  cp port/3ds/build/3ds/PaperMario3DS.3dsx /path/to/sdcard/3ds/"
    echo ""
else
    echo ""
    echo "[!] Build did not produce expected output."
    exit 1
fi
