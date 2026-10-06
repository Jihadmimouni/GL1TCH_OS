# syntax pinned by digest (see "Updating the pinned base image" below for why,
# and how to intentionally refresh this).
# syntax=docker/dockerfile:1@sha256:4edf897a3ffa55b89f906fc8cc78afdb3f1834cc9c7083565e611a8a7d5fe99e

########################################################################
# Stage 1: build toolchain (NASM, GCC, Open Watcom, mtools/dosfstools)
# and produce build/main_floppy.img
########################################################################
# Pinned by digest, not just ":24.04" - a floating tag makes BuildKit
# contact the registry to re-resolve it on *every* build (a small metadata
# call, but still network access, even when every layer is already cached
# and nothing changed). A digest is immutable, so BuildKit can match it
# against the local image store and skip the network entirely. See
# "Updating the pinned base image" below.
FROM ubuntu:24.04@sha256:534baea6a22c03a63003dbc8dbe78fe34bc0d7e595d9a9dc9834884ff530eb55 AS builder

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
# The snapshot asset name has changed before, so by default resolve it from
# the GitHub API; pass --build-arg WATCOM_SNAPSHOT_URL=... to pin a URL.
ARG WATCOM_SNAPSHOT_URL=
RUN mkdir -p /usr/bin/watcom \
    && url="$WATCOM_SNAPSHOT_URL" \
    && if [ -z "$url" ]; then \
         url="$(curl -fsSL https://api.github.com/repos/open-watcom/open-watcom-v2/releases/tags/Current-build \
                | grep -o '"browser_download_url": *"[^"]*snapshot[^"]*\.tar\.xz"' \
                | head -n1 | sed 's/.*"\(https[^"]*\)"/\1/')"; \
       fi \
    && url="${url:-https://github.com/open-watcom/open-watcom-v2/releases/download/Current-build/ow-snapshot.tar.xz}" \
    && echo "Fetching Open Watcom from $url" \
    && curl -fsSL "$url" -o /tmp/owsnapshot.tar.xz \
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
# Same pinned digest as the builder stage - same reasoning.
FROM ubuntu:24.04@sha256:534baea6a22c03a63003dbc8dbe78fe34bc0d7e595d9a9dc9834884ff530eb55 AS runtime

RUN apt-get update && apt-get install -y --no-install-recommends \
        qemu-system-x86 \
    && rm -rf /var/lib/apt/lists/* \
    && useradd --create-home --shell /bin/bash gl1tch

# Owned by the runtime user: QEMU opens floppy images read-write.
COPY --from=builder --chown=gl1tch:gl1tch /build/build/main_floppy.img /os/main_floppy.img

WORKDIR /os
USER gl1tch

# Default: boot in QEMU's curses display so the OS's VGA text-mode output
# (BIOS int 10h teletype) is visible straight in the terminal, with no X11
# or VNC needed. Override the CMD (e.g. `-display sdl`) if you have a
# display available and want graphics instead.
ENTRYPOINT ["qemu-system-i386", "-drive", "file=/os/main_floppy.img,format=raw,if=floppy"]
CMD ["-display", "curses"]

########################################################################
# Updating the pinned base image
#
# `docker build -t gl1tch-os .` makes NO network calls once the image
# above is cached locally - that's the point of pinning by digest instead
# of a floating tag. To intentionally pick up a newer ubuntu:24.04 (and
# accept that one-time network cost), run:
#
#   docker pull ubuntu:24.04
#   docker image inspect ubuntu:24.04 --format '{{index .RepoDigests 0}}'
#
# then replace the sha256:... value on both FROM lines above (and the
# docker/dockerfile:1 syntax line, if you want the latest Dockerfile
# frontend too - same idea, just `docker pull docker/dockerfile:1` first).
########################################################################
