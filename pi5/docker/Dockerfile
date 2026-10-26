# Build environment for everything that runs on the workstation:
#   - fsck.cramfs to unpack the RX3 rootfs (scripts/prepare-assets.sh)
#   - the soft-float (armel) cross toolchain for the shims and DirectFB
#   - flux/DirectFB build tools (autotools, g++, patchelf)
#   - qemu-user for the shims' ARM test binaries (make test)
#
# The shims link against the XDJ-RX3 glibc 2.13, so the toolchain is
# arm-linux-gnueabi (soft-float), NOT arm-linux-gnueabihf.
FROM debian:bookworm

RUN apt-get update && apt-get install -y --no-install-recommends \
        gcc-arm-linux-gnueabi libc6-dev-armel-cross binutils-arm-linux-gnueabi \
        autoconf automake libtool pkg-config patchelf \
        gcc g++ make git ca-certificates file python3 patch \
        qemu-user util-linux xz-utils \
    && rm -rf /var/lib/apt/lists/*

ENV CROSS=arm-linux-gnueabi-
