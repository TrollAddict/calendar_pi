# Cross-compilation toolchain file for Raspberry Pi Zero W (armv6, armhf).
#
# Usage:
#   cmake -B build -S . \
#         -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-rpi.cmake \
#         -DRPI_SYSROOT=$HOME/rpi-sysroot
#
# See README.md for how to install the arm-linux-gnueabihf cross compiler
# and populate RPI_SYSROOT from your actual Pi.

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(CMAKE_C_COMPILER arm-linux-gnueabihf-gcc)
set(CMAKE_CXX_COMPILER arm-linux-gnueabihf-g++)

if(NOT DEFINED RPI_SYSROOT)
    set(RPI_SYSROOT "$ENV{HOME}/rpi-sysroot" CACHE PATH "Path to a copy of the Pi's root filesystem (headers + libs)")
endif()

set(CMAKE_SYSROOT ${RPI_SYSROOT})
set(CMAKE_FIND_ROOT_PATH ${RPI_SYSROOT})

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Pi Zero W is armv6 (ARM1176JZF-S) with a VFPv2 hard-float FPU.
# -marm forces full ARM (not Thumb) encoding: armv6 only has Thumb-1, and
# GCC can't do hard-float VFP argument passing in Thumb-1 ("sorry,
# unimplemented: Thumb-1 'hard-float' VFP ABI" otherwise).
#
# -B<sysroot>/usr/lib/arm-linux-gnueabihf forces the crt startup objects
# (crt1.o/Scrt1.o/...) to come from the Pi's own sysroot too. Despite
# --sysroot being set, crossbuild-essential-armhf's gcc still resolves
# these from its own bundled default sysroot (/usr/arm-linux-gnueabihf),
# which targets Debian's generic armhf baseline (ARMv7+VFPv3) rather than
# this board's real ARMv6 core -- linking against the wrong startup
# objects produces a binary that segfaults immediately on boot, before
# main() ever runs. -B makes the linker search the real sysroot first.
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -march=armv6 -mfpu=vfp -mfloat-abi=hard -marm -B${RPI_SYSROOT}/usr/lib/arm-linux-gnueabihf" CACHE STRING "" FORCE)
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -B${RPI_SYSROOT}/usr/lib/arm-linux-gnueabihf" CACHE STRING "" FORCE)

# Point pkg-config at the sysroot's .pc files instead of the host's.
set(ENV{PKG_CONFIG_SYSROOT_DIR} ${RPI_SYSROOT})
set(ENV{PKG_CONFIG_LIBDIR} "${RPI_SYSROOT}/usr/lib/arm-linux-gnueabihf/pkgconfig:${RPI_SYSROOT}/usr/lib/pkgconfig:${RPI_SYSROOT}/usr/share/pkgconfig")
