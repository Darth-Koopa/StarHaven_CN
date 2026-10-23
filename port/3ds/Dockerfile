# ==========================================================================
# StarHaven / Paper Mario 3DS build environment
#
# Use the official, prebuilt devkitPro ARM image. Do not install packages
# with dkp-pacman during CI: the package mirror can deny GitHub-hosted runner
# requests (HTTP 403), and package resolution makes builds non-reproducible.
# The date tag pins the toolchain image used by this build.
#
# The image contains devkitARM, libctru, citro3d, 3dsxtool and the other
# standard 3DS tools. Host-side ROM extraction and MIPS tools run on the
# GitHub Actions runner; this image is only for the 3DS cross-build/package.
# ==========================================================================
FROM devkitpro/devkitarm:20260610

LABEL org.opencontainers.image.title="StarHaven iQue 3DS builder"

ENV DEVKITPRO=/opt/devkitpro \
    DEVKITARM=/opt/devkitpro/devkitARM \
    DEVKITPPC=/opt/devkitpro/devkitPPC \
    PATH=/opt/devkitpro/devkitARM/bin:/opt/devkitpro/tools/bin:${PATH}

# Fail during image build if the selected upstream image ever changes and
# no longer includes the toolchain pieces this project relies on.
RUN set -eu; \
    test -x "$DEVKITARM/bin/arm-none-eabi-gcc"; \
    test -f "$DEVKITARM/3ds_rules"; \
    test -f "$DEVKITPRO/libctru/include/3ds.h"; \
    command -v 3dsxtool; \
    arm-none-eabi-gcc --version

WORKDIR /project
CMD ["make", "-C", "/project/port/3ds"]
