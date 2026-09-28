# syntax=docker/dockerfile:1

########################################################################
# Stage 1: build toolchain (NASM, GCC, Open Watcom, mtools/dosfstools)
# and produce build/main_floppy.img
########################################################################
FROM ubuntu:24.04 AS builder

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        nasm \
        make \
        mtools \
        dosfstools \
        curl \
        xz-utils \
        ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Open Watcom v2 (needed for the 16-bit stage2 C compiler/linker).
# It ships as a snapshot tarball of prebuilt binaries for every supported
# host/target - no installer needed, we just need binl64 (64-bit Linux) on
# PATH, matching the path the project's makefiles already hardcode
# (/usr/bin/watcom/binl64/wcc and /usr/bin/watcom/binl64/wlink).
ARG WATCOM_SNAPSHOT_URL=https://github.com/open-watcom/open-watcom-v2/releases/download/Current-build/owsnapshot.tar.xz
RUN mkdir -p /usr/bin/watcom \
    && curl -fsSL "$WATCOM_SNAPSHOT_URL" -o /tmp/owsnapshot.tar.xz \
    && tar -xf /tmp/owsnapshot.tar.xz -C /usr/bin/watcom \
    && rm -f /tmp/owsnapshot.tar.xz

ENV WATCOM=/usr/bin/watcom
ENV PATH="${WATCOM}/binl64:${PATH}"

WORKDIR /build
COPY . .

RUN make

########################################################################
# Stage 2: minimal runtime image - just QEMU and the built floppy image,
# so the produced image can be run directly with `docker run`.
########################################################################
FROM ubuntu:24.04 AS runtime

RUN apt-get update && apt-get install -y --no-install-recommends \
        qemu-system-x86 \
    && rm -rf /var/lib/apt/lists/* \
    && useradd --create-home --shell /bin/bash gl1tch

COPY --from=builder /build/build/main_floppy.img /os/main_floppy.img

WORKDIR /os
USER gl1tch

# Default: boot in QEMU's curses display so the OS's VGA text-mode output
# (BIOS int 10h teletype) is visible straight in the terminal, with no X11
# or VNC needed. Override the CMD (e.g. `-display sdl`) if you have a
# display available and want graphics instead.
ENTRYPOINT ["qemu-system-i386", "-fda", "/os/main_floppy.img"]
CMD ["-display", "curses"]
