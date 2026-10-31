# xv6 lab: build, run and debug environment.
#
# Students and TAs use this same image, so everyone builds with the same
# toolchain and qemu.  Two things keep it reproducible:
#   - the base image is pinned by digest (a multi-arch index, so it builds
#     natively on linux/amd64 and linux/arm64, e.g. Apple Silicon);
#   - optionally, apt reads an Ubuntu archive snapshot so that a rebuild
#     installs exactly the same package versions:
#       docker build --build-arg APT_SNAPSHOT=20261015T000000Z ...
#     It is off by default because snapshot.ubuntu.com is slow from Taiwan
#     (>20 min vs ~1.5 min).  Better: the TAs build once and publish the
#     image, and devcontainer.json / ./lab pull it.
#
# Use it either way:
#   VS Code:  "Dev Containers: Reopen in Container" (see devcontainer.json)
#   CLI:      ./lab shell        (or ./lab make qemu)
FROM ubuntu:26.04@sha256:c098726dd9b9733f3e885f8de0ef375206a311b0a317eadc40d347b1115fb944

ARG APT_SNAPSHOT=

ENV DEBIAN_FRONTEND=noninteractive \
    LANG=C.UTF-8 \
    LC_ALL=C.UTF-8

# build-essential  host gcc for mkfs, plus make and perl (user/usys.pl)
# gcc-riscv64-...  cross compiler; the Makefile finds riscv64-linux-gnu-*
# qemu-system-riscv  qemu-system-riscv64 >= 7.2 (Makefile checks; needs bc)
# gdb-multiarch    make qemu-gdb
# python3          test-xv6.py
# git zip unzip    branches, and the submission zip
# clang-format     make fmt
# tmux             qemu and gdb side by side in one terminal
RUN apt-get update ${APT_SNAPSHOT:+--snapshot "$APT_SNAPSHOT"} \
    && apt-get install -y --no-install-recommends \
        build-essential \
        gcc-riscv64-linux-gnu \
        qemu-system-riscv \
        gdb-multiarch \
        python3 \
        bc \
        git \
        zip \
        unzip \
        clang-format \
        tmux \
        less \
        ca-certificates \
        bash-completion \
    && rm -rf /var/lib/apt/lists/*

# gdb: default to riscv and allow the .gdbinit that "make qemu-gdb" writes
# in the project directory.  System-wide, so it works for any uid.
RUN printf 'set architecture riscv:rv64\nadd-auto-load-safe-path /\n' \
        > /etc/gdb/gdbinit.d/xv6.gdb

# git: the project is bind-mounted from the host and is owned by a uid that
# usually differs from the container's (Docker Desktop, -u $(id -u)); without
# this git refuses to run there ("dubious ownership").
RUN git config --system --add safe.directory '*'

# Show the current git branch in the prompt.  Uses git's
# own git-sh-prompt, so nothing is downloaded from third parties.
RUN printf '%s\n' \
        '' \
        '# xv6 lab' \
        '. /usr/lib/git-core/git-sh-prompt' \
        'GIT_PS1_SHOWDIRTYSTATE=1' \
        "PS1='\\[\\e[1;34m\\]\\w\\[\\e[0;33m\\]\$(__git_ps1 \" (%s)\")\\[\\e[0m\\] \\$ '" \
        'echo "xv6 lab: make qemu (exit: Ctrl-a x) | make qemu-gdb"' \
        >> /etc/bash.bashrc

# Non-root user for interactive use (VS Code remoteUser).  The ubuntu image
# already has uid 1000 "ubuntu"; ./lab runs as the host uid instead.
USER ubuntu
WORKDIR /home/ubuntu
CMD ["bash"]
