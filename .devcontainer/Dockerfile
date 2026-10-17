FROM mcr.microsoft.com/devcontainers/base:ubuntu-24.04@sha256:d94c97dd9cacf183d0a6fd12a8e87b526e9e928307674ae9c94139139c0c6eae

ARG DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        build-essential \
        ca-certificates \
        clang-18=1:18.1.3-1ubuntu1 \
        clang-tidy-18=1:18.1.3-1ubuntu1 \
        libclang-rt-18-dev=1:18.1.3-1ubuntu1 \
        ccache \
        device-tree-compiler \
        file \
        gperf \
        git \
        libmagic1 \
        libsdl2-dev \
        pkg-config \
        python3 \
        python3-dev \
        python3-pip \
        python3-tk \
        python3-venv \
        ripgrep \
        wget \
        xz-utils \
    && rm -rf /var/lib/apt/lists/* \
    && install -d -o vscode -g vscode /opt/crux-cache \
    && python3 -m pip install --break-system-packages --no-cache-dir \
        west==1.4.0 \
        pytest==8.3.5

ENV CC=clang-18 \
    CXX=clang++-18
