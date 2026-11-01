# Reroll Module Build Environment
# Targets: Ableton Move (aarch64 Linux)

# Pinned to amd64: the Debian aarch64 cross-toolchain packages are built for
# x86 hosts. On Apple Silicon, Docker Desktop runs this under Rosetta.
FROM --platform=linux/amd64 debian:bookworm

RUN apt-get update && apt-get install -y \
    gcc-aarch64-linux-gnu \
    g++-aarch64-linux-gnu \
    make \
    file \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build

# Set cross-compilation environment
ENV CROSS_PREFIX=aarch64-linux-gnu-
ENV CC=aarch64-linux-gnu-gcc
ENV CXX=aarch64-linux-gnu-g++
