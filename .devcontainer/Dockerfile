FROM mcr.microsoft.com/devcontainers/base:ubuntu-24.04@sha256:d94c97dd9cacf183d0a6fd12a8e87b526e9e928307674ae9c94139139c0c6eae

ARG DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        bash-completion \
        build-essential \
        ca-certificates \
        clang-18=1:18.1.3-1ubuntu1 \
        clangd-18=1:18.1.3-1ubuntu1 \
        clang-tidy-18=1:18.1.3-1ubuntu1 \
        libclang-rt-18-dev=1:18.1.3-1ubuntu1 \
        ccache \
        device-tree-compiler \
        file \
        gdb \
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
    && ln -sf /usr/bin/clangd-18 /usr/local/bin/clangd \
    && rm -rf /var/lib/apt/lists/* \
    && install -d -o vscode -g vscode /opt/crux-cache \
    && install -d -o vscode -g vscode /home/vscode/.vscode-server /home/vscode/.vscode-server/extensions

# clangd user config: Zephyr headers sit outside the repository, where .clangd does not apply.
# It removes the GCC options that Zephyr adds to native_sim builds; keep the list equal to the
# unconditional fragment of .clangd (tests/build/test_vscode_workspace.py checks it).
RUN install -d /home/vscode/.config/clangd \
    && printf '%s\n' \
        'CompileFlags:' \
        '  Remove:' \
        '    - -fno-reorder-functions' \
        '    - -fno-defer-pop' \
        '    - --param=min-pagesize=0' \
        '    - -fno-freestanding' \
        '    - -fno-printf-return-value' \
        > /home/vscode/.config/clangd/config.yaml \
    && chown -R vscode:vscode /home/vscode/.config

# Your organisation's root CA, for networks where a proxy re-signs HTTPS. Every
# certificate in a .crt, .pem or .cer file that a developer placed in
# .devcontainer/certs/ is added to the system bundle before any download below; with
# none there, no certificate is installed. Git never carries a certificate (see
# .devcontainer/certs/README.md). The folder is bind-mounted into the one RUN step
# that reads it (BuildKit): the image stores the installed CA certificates and no
# other file from the folder. The ENV
# lines point tools that keep their own CA list at the system bundle, at build time
# and at run time.
ENV REQUESTS_CA_BUNDLE=/etc/ssl/certs/ca-certificates.crt \
    CURL_CA_BUNDLE=/etc/ssl/certs/ca-certificates.crt \
    SSL_CERT_FILE=/etc/ssl/certs/ca-certificates.crt \
    GIT_SSL_CAINFO=/etc/ssl/certs/ca-certificates.crt \
    PIP_CERT=/etc/ssl/certs/ca-certificates.crt \
    NODE_EXTRA_CA_CERTS=/etc/ssl/certs/ca-certificates.crt
RUN --mount=type=bind,source=.devcontainer/certs,target=/tmp/crux-certs \
    --mount=type=bind,source=.devcontainer/install-org-ca.sh,target=/tmp/crux-install-org-ca.sh \
    sh /tmp/crux-install-org-ca.sh /tmp/crux-certs

# 32-bit native_sim (the default native_sim/native target) needs a host compiler with
# multilib support; scripts/test-storage-simulators.sh builds it. Zephyr's Ubuntu host
# packages include gcc-multilib and g++-multilib, which do not exist on arm64, so they are
# installed only in an amd64 image (tests/build/test_devcontainer_multilib.py checks it).
RUN if [ "$(dpkg --print-architecture)" = amd64 ]; then \
        apt-get update \
        && apt-get install -y --no-install-recommends gcc-multilib g++-multilib \
        && rm -rf /var/lib/apt/lists/*; \
    fi

RUN python3 -m pip install --break-system-packages --no-cache-dir \
        west==1.4.0 \
        pytest==8.3.5

ENV CC=clang-18 \
    CXX=clang++-18

# SEGGER J-Link software for flashing and debugging a board (see ADR-0007 and
# docs/board-jlink-usbipd.md). The package comes from the first of: a file the
# developer placed in .devcontainer/jlink/packages/; CRUX_JLINK_URL, a copy that
# an organisation hosts, checked against CRUX_JLINK_DEB_SHA256; the version
# pinned below, downloaded from SEGGER's download endpoint and checked against
# the pinned SHA-256. That download accepts SEGGER's terms of use (see
# install-jlink.sh). CRUX_JLINK=auto (devcontainer.json) installs it and, when
# none of the three gives a package, prints a warning and builds the image
# without it, so Host and native_sim work for everyone; 1 requires it; 0 skips it.
# CI and other automated image builds pass --build-arg CRUX_JLINK=0, so they
# never download from SEGGER (docs/board-jlink-usbipd.md).
# To bump J-Link, change the two CRUX_JLINK_SEGGER_* values together.
ARG CRUX_JLINK=auto
ARG CRUX_JLINK_DEB=
ARG CRUX_JLINK_DEB_SHA256=
ARG CRUX_JLINK_URL=
ARG CRUX_JLINK_SEGGER_VERSION=V996a
ARG CRUX_JLINK_SEGGER_SHA256=e23591160efa39e0f9e184031195a594d962853a801a1261b168a02644ab2a0a
COPY .devcontainer/jlink/ /tmp/crux-jlink/
RUN if [ "$CRUX_JLINK" != 0 ]; then \
        sh /tmp/crux-jlink/install-jlink.sh /tmp/crux-jlink/packages \
        && rm -rf /var/lib/apt/lists/*; \
    fi \
    && rm -rf /tmp/crux-jlink
