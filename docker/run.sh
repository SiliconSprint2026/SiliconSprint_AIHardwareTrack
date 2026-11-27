#!/usr/bin/env bash
# Run a command (or open a shell) inside the TENSOR-CORE toolchain container.
#
#   bash docker/run.sh                 shell, with this repo at /work
#   bash docker/run.sh make test       run one command and exit
#
# The first run builds the image (about 15 minutes). Linux, macOS, Windows (Git Bash;
# from PowerShell use docker\run.ps1).
set -euo pipefail

cd "$(dirname "$0")/.."
repo=$(pwd -W 2>/dev/null || pwd)   # pwd -W gives C:/... on Git Bash

hash=$(cat docker/Dockerfile docker/requirements-kit.txt | sha256sum | cut -c1-12)
image="tensor-core:kit-${hash}"

if ! docker image inspect "$image" >/dev/null 2>&1; then
  echo ">> building the toolchain image $image (first time only, about 15 minutes)"
  docker build -t "$image" -f docker/Dockerfile .
fi

opts=(--rm -v "${repo}:/work" -w /work)
[ -t 0 ] && [ -t 1 ] && opts+=(-it)
if [ "$(uname -s)" = Linux ]; then
  opts+=(--user "$(id -u):$(id -g)" -e HOME=/tmp)
fi

[ $# -eq 0 ] && set -- bash
MSYS_NO_PATHCONV=1 exec docker run "${opts[@]}" "$image" "$@"
