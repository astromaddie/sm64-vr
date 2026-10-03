# The pinned Linux build environment for SM64 VR, after the WipEout VR and F-Zero X ports':
# Ubuntu 22.04 (glibc 2.35, the release floor), gcc-12, CMake 3.31.6, SDL2 2.30.x built from
# source with x11, and the OpenXR loader from OpenXR-SDK release-1.1.63, built as a static
# library. The image builds for the Docker host's architecture, so on an Apple Silicon Mac
# (Colima) it is arm64, the Steam Frame's.
#
#   docker build -t sm64-vr-linux -f tools/docker/linux.Dockerfile tools/docker
#   tools/docker/build-linux.sh
ARG BASE_IMAGE=ubuntu:22.04
FROM ${BASE_IMAGE}

ENV DEBIAN_FRONTEND=noninteractive \
    CC=gcc-12 \
    CXX=g++-12

ARG CMAKE_VERSION=3.31.6
ARG CMAKE_SHA256_AARCH64=b4cc788d63112b2749b40627e719eb5d3b8ed8f00c36d77189f4019cfe64bc9e
ARG CMAKE_SHA256_X86_64=5a1133ff103c71eb5120e2cc3de922733e7d8a26a98ae716397e8676adb367bf
ARG SDL2_VERSION=2.30.11
ARG SDL2_SHA256=8b8d4aef2038533da814965220f88f77d60dfa0f32685f80ead65e501337da7f
# OpenXR-SDK release-1.1.63 (commit f2448a8797c85814aa892efc1ab8707900fbcc78)
ARG OPENXR_VERSION=1.1.63
ARG OPENXR_SHA256=5b8dac0608f49498fe8e95f562fe6bcf0bc781e561f137348a8c251d06a88b6f

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential gcc-12 g++-12 ninja-build pkg-config \
        git ca-certificates wget file rsync bsdmainutils \
        python3 zlib1g-dev libcurl4-openssl-dev libcapstone-dev \
        libgl1-mesa-dev libopengl-dev libegl1-mesa-dev \
        libasound2-dev libpulse-dev libudev-dev libdbus-1-dev \
        libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev \
        libxinerama-dev libxss-dev libxkbcommon-dev \
        libwayland-dev wayland-protocols libdrm-dev libgbm-dev \
        xvfb xauth libgl1-mesa-dri mesa-utils \
    && rm -rf /var/lib/apt/lists/* \
    && update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-12 100 \
    && update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-12 100 \
    && update-alternatives --install /usr/bin/cpp cpp /usr/bin/cpp-12 100 \
    && git config --global --add safe.directory '*'

RUN set -eux; \
    arch="$(uname -m)"; \
    case "$arch" in aarch64) sha="${CMAKE_SHA256_AARCH64}" ;; x86_64) sha="${CMAKE_SHA256_X86_64}" ;; *) exit 1 ;; esac; \
    wget -q "https://github.com/Kitware/CMake/releases/download/v${CMAKE_VERSION}/cmake-${CMAKE_VERSION}-linux-${arch}.tar.gz"; \
    echo "${sha}  cmake-${CMAKE_VERSION}-linux-${arch}.tar.gz" | sha256sum -c -; \
    mkdir -p /opt/cmake; \
    tar -xzf "cmake-${CMAKE_VERSION}-linux-${arch}.tar.gz" -C /opt/cmake --strip-components=1; \
    rm "cmake-${CMAKE_VERSION}-linux-${arch}.tar.gz"
ENV PATH=/opt/cmake/bin:$PATH

# SDL2 with x11: the Frame's video driver is x11 (gamescope's Xwayland), and the XR GL binding
# takes the GLX context from SDL's x11 window.
RUN set -eux; \
    cd /tmp; \
    wget -q "https://github.com/libsdl-org/SDL/releases/download/release-${SDL2_VERSION}/SDL2-${SDL2_VERSION}.tar.gz"; \
    echo "${SDL2_SHA256}  SDL2-${SDL2_VERSION}.tar.gz" | sha256sum -c -; \
    tar -xzf "SDL2-${SDL2_VERSION}.tar.gz"; \
    cmake -S "SDL2-${SDL2_VERSION}" -B build-sdl2 -GNinja -DCMAKE_BUILD_TYPE=Release \
        -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TEST=OFF; \
    cmake --build build-sdl2 --target install; \
    ldconfig; \
    rm -rf /tmp/*

# The OpenXR loader, static, with its headers, in /opt/openxr. The Makefile's VR=1 build links
# /opt/openxr/lib/libopenxr_loader.a, so the package ships no loader library.
RUN set -eux; \
    cd /tmp; \
    wget -q -O openxr.tar.gz "https://github.com/KhronosGroup/OpenXR-SDK/archive/refs/tags/release-${OPENXR_VERSION}.tar.gz"; \
    echo "${OPENXR_SHA256}  openxr.tar.gz" | sha256sum -c -; \
    tar -xzf openxr.tar.gz; \
    cmake -S "OpenXR-SDK-release-${OPENXR_VERSION}" -B build-openxr -GNinja -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/opt/openxr -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
        -DDYNAMIC_LOADER=OFF -DBUILD_API_LAYERS=OFF -DBUILD_TESTS=OFF -DBUILD_CONFORMANCE_TESTS=OFF \
        -DBUILD_WITH_SYSTEM_JSONCPP=OFF -DBUILD_WITH_XCB_HEADERS=OFF -DBUILD_WITH_WAYLAND_HEADERS=OFF; \
    cmake --build build-openxr --target install; \
    echo "OpenXR-SDK release-${OPENXR_VERSION} sha256 ${OPENXR_SHA256}" > /opt/openxr/PINNED; \
    rm -rf /tmp/*

WORKDIR /src
