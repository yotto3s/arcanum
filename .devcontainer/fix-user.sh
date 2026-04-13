#!/usr/bin/env bash
# Align the in-container 'dev' user's UID/GID with the host user's, based on
# ownership of the bind-mounted workspace. Runs as root via onCreateCommand.
set -euo pipefail

HOST_UID=$(stat -c %u /workspace)
HOST_GID=$(stat -c %g /workspace)

# Refuse to remap the dev user to root or other unsafe IDs. /workspace owned
# by root usually means the bind mount is misconfigured (Docker Desktop on
# some hosts, root-cloned repos, etc.); silently turning the in-container
# user into root would mask that and break the security model.
if ! [[ "$HOST_UID" =~ ^[0-9]+$ ]] || ! [[ "$HOST_GID" =~ ^[0-9]+$ ]] \
   || [ "$HOST_UID" -eq 0 ] || [ "$HOST_GID" -eq 0 ]; then
    echo "fix-user.sh: refusing to remap 'dev' to uid=$HOST_UID gid=$HOST_GID (must be non-zero numeric)." >&2
    exit 1
fi

CUR_UID=$(id -u dev)
CUR_GID=$(id -g dev)

if [ "$CUR_GID" != "$HOST_GID" ]; then
    # Free the GID if some other group already holds it.
    if getent group "$HOST_GID" >/dev/null && [ "$(getent group "$HOST_GID" | cut -d: -f1)" != "dev" ]; then
        groupmod -g 65500 "$(getent group "$HOST_GID" | cut -d: -f1)"
    fi
    groupmod -g "$HOST_GID" dev
fi

if [ "$CUR_UID" != "$HOST_UID" ]; then
    if getent passwd "$HOST_UID" >/dev/null && [ "$(getent passwd "$HOST_UID" | cut -d: -f1)" != "dev" ]; then
        usermod -u 65500 "$(getent passwd "$HOST_UID" | cut -d: -f1)"
    fi
    usermod -u "$HOST_UID" -g "$HOST_GID" dev
fi

chown -R "$HOST_UID:$HOST_GID" /home/dev 2>/dev/null || true
