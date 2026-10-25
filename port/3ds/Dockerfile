# ============================================================================
# Paper Mario 3DS - Docker Build Environment
# ============================================================================
# This Dockerfile creates a container with the full devkitPro 3DS toolchain.
#
# Usage:
#   docker build -t papermario-3ds .
#   docker run --rm -v $(pwd):/project papermario-3ds
#
# The devkitPro image includes:
#   - devkitARM (arm-none-eabi-gcc)
#   - libctru (3DS OS library)
#   - citro3d (3DS GPU library)
#   - 3dsxtool, smdhtool, bannertool
# ============================================================================

FROM devkitpro/toolchain-base:latest

# Source the devkit-env so tools are in PATH
ENV DEVKITPRO=/opt/devkitpro
ENV DEVKITARM=/opt/devkitpro/devkitARM
ENV DEVKITPPC=/opt/devkitpro/devkitPPC
ENV PATH=$DEVKITARM/bin:$DEVKITPRO/tools/bin:$PATH

# Install the 3DS development toolchain
RUN dkp-pacman -Syyu --noconfirm 3ds-dev

# The update check does its own TLS (the 3DS's SSL module can't reach GitHub)
RUN dkp-pacman -S --noconfirm --needed 3ds-curl 3ds-mbedtls

# Verify tools are available
RUN arm-none-eabi-gcc --version && which 3dsxtool && echo "OK"

# Set working directory
WORKDIR /project

# Default command: build the 3DS port
CMD ["make", "-C", "/project/port/3ds"]
