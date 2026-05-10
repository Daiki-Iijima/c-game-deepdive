# syntax=docker/dockerfile:1.6
# c-game-deepdive 共通開発環境
# Apple Silicon と Intel Mac で挙動を揃えるため amd64 を強制する (compose.yml 側でも platform 指定)
FROM --platform=linux/amd64 ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive \
    LANG=C.UTF-8 \
    LC_ALL=C.UTF-8 \
    TERM=xterm-256color

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    gcc \
    clang \
    gdb \
    make \
    cmake \
    valgrind \
    strace \
    ltrace \
    binutils \
    elfutils \
    file \
    linux-tools-generic \
    linux-tools-common \
    less \
    vim \
    git \
    ca-certificates \
    locales \
    asciinema \
    xxd \
    && rm -rf /var/lib/apt/lists/*

# perf は linux-tools-generic から symlink する (kernel headers なしで stat/record/report は動く)
RUN ln -sf "$(ls /usr/lib/linux-tools/*/perf | head -n1)" /usr/local/bin/perf || true

WORKDIR /workspace
CMD ["/bin/bash"]
