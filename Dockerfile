FROM debian:bookworm

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    gcc \
    binutils \
    nasm \
    make \
    grub-common \
    grub-pc-bin \
    xorriso \
    mtools \
    qemu-system-x86 \
    gdb \
    file \
    ca-certificates \
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /work

CMD ["/bin/bash"]
