ARG BASE_IMAGE=ghcr.io/yotto3s/arcanum-base:latest
FROM ${BASE_IMAGE}

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
        ripgrep \
        fd-find \
        fzf \
        bat \
        jq \
        tmux \
        zsh \
        htop \
        tree \
        less \
        man-db \
        openssh-client \
        gnupg \
        vim \
    && ln -sf /usr/bin/fdfind /usr/local/bin/fd \
    && ln -sf /usr/bin/batcat /usr/local/bin/bat \
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/*

# GitHub CLI (official apt repo)
RUN curl -fsSL https://cli.github.com/packages/githubcli-archive-keyring.gpg \
        | gpg --dearmor -o /usr/share/keyrings/githubcli-archive-keyring.gpg \
 && chmod go+r /usr/share/keyrings/githubcli-archive-keyring.gpg \
 && echo "deb [arch=$(dpkg --print-architecture) signed-by=/usr/share/keyrings/githubcli-archive-keyring.gpg] https://cli.github.com/packages stable main" \
        > /etc/apt/sources.list.d/github-cli.list \
 && apt-get update \
 && apt-get install -y --no-install-recommends gh \
 && apt-get clean && rm -rf /var/lib/apt/lists/*

# Placeholder dev user (UID/GID 1000); UID/GID are aligned to the host at
# container setup by onCreateCommand invoking fix-user.sh — no ENTRYPOINT.
RUN groupadd --gid 1000 dev \
 && useradd  --uid 1000 --gid 1000 -m -s /bin/bash dev \
 && echo "dev ALL=(ALL) NOPASSWD:ALL" > /etc/sudoers.d/dev \
 && chmod 0440 /etc/sudoers.d/dev

# Roswell setup + Claude Code (native installer, official method).
USER dev
ENV PATH=/home/dev/.local/bin:${PATH}
RUN ros setup \
 && ros install sbcl-bin \
 && ros use sbcl-bin \
 && curl -fsSL https://claude.ai/install.sh | bash
USER root

COPY fix-user.sh /usr/local/bin/fix-user.sh
RUN chmod +x /usr/local/bin/fix-user.sh

WORKDIR /workspace
