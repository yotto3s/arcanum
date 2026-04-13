FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive \
    LANG=C.UTF-8 \
    LC_ALL=C.UTF-8

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        cmake \
        ninja-build \
        git \
        pkg-config \
        python3 \
        python3-pip \
        ccache \
        clang \
        lld \
        llvm-dev \
        libclang-dev \
        clang-tools \
        libstdc++-13-dev \
        why3 \
        z3 \
        cvc4 \
        libgtest-dev \
        googletest \
        curl \
        ca-certificates \
        unzip \
        xz-utils \
        gosu \
        sudo \
        automake \
        libcurl4-openssl-dev \
        zlib1g-dev \
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/*

# Free UID 1000 / "ubuntu" user that ships in ubuntu:24.04 so the devcontainer
# onCreateCommand (fix-user.sh) can remap its placeholder user to host UID
# without collision.
RUN userdel -r ubuntu 2>/dev/null || true

# Build and install Roswell from source. Per-user `ros setup` and SBCL
# installation are deferred to the devcontainer image so SBCL ends up under
# the developer's home, not under /root.
RUN git clone -b release --depth 1 https://github.com/roswell/roswell.git /tmp/roswell \
 && cd /tmp/roswell \
 && ./bootstrap \
 && ./configure \
 && make -j"$(nproc)" \
 && make install \
 && cd / && rm -rf /tmp/roswell

WORKDIR /workspace
