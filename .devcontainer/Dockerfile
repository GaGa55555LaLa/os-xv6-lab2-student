# xv6-riscv dev environment.
# Ubuntu 26.04 matches the CI image in .github/workflows/test.yml, so the
# toolchain and clang-format versions are the same ones CI checks against.
# The base image is multi-arch: this builds natively on both linux/amd64
# (x86, Intel Mac) and linux/arm64 (Apple Silicon).
FROM mcr.microsoft.com/devcontainers/base:ubuntu26.04

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        build-essential \
        gcc-riscv64-linux-gnu \
        qemu-system-riscv \
        gdb-multiarch \
        clang-format \
        bc \
        python3 \
    && rm -rf /var/lib/apt/lists/*

# gdb needs to default to riscv and be allowed to load the .gdbinit
# that "make qemu-gdb" generates in the project directory.
RUN printf 'set architecture riscv:rv64\nadd-auto-load-safe-path /\n' > /home/vscode/.gdbinit \
    && chown vscode:vscode /home/vscode/.gdbinit \
    && cp /home/vscode/.gdbinit /root/.gdbinit

# oh-my-bash, for the interactive shell only: a themed prompt that shows
# the current git branch, plus completions and a few aliases.  Shared
# between root and vscode instead of installed per user, and pinned so
# that rebuilding the image does not quietly change it.
#
# None of this affects builds: a non-interactive shell never reads
# .bashrc, so `docker run ... make` behaves exactly as before.
RUN git clone --filter=blob:none -q https://github.com/ohmybash/oh-my-bash.git /opt/oh-my-bash \
    && git -C /opt/oh-my-bash checkout -q abf8461 \
    && rm -rf /opt/oh-my-bash/.git

RUN for home in /root /home/vscode; do \
        printf '%s\n' \
            '' \
            '# oh-my-bash' \
            'export OSH=/opt/oh-my-bash' \
            'OSH_THEME="font"' \
            'completions=(git ssh)' \
            'aliases=(general)' \
            'plugins=(git bashmarks)' \
            'source "$OSH"/oh-my-bash.sh' \
            >> "$home/.bashrc"; \
    done \
    && chown vscode:vscode /home/vscode/.bashrc
