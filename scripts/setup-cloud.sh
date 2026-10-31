#!/usr/bin/env bash
# Reproduce the Debian 13 cloud build without modifying the system installation.
set -euo pipefail

project_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
tools_root="${DUALIR_TOOLS_ROOT:-/workspace/dualir-tools}"
deps_root="${DUALIR_DEPS_ROOT:-/workspace/dualir-deps}"

python3 -m venv "$tools_root"
"$tools_root/bin/python" -m pip install --disable-pip-version-check cmake==3.31.6 ninja==1.11.1.4
export PATH="$tools_root/bin:$PATH"
export CMAKE_BUILD_PARALLEL_LEVEL="${CMAKE_BUILD_PARALLEL_LEVEL:-2}"
bash "$project_root/scripts/setup-linux-deps.sh"
source "$deps_root/env.sh"
cd "$project_root"
cmake --preset linux-debug
cmake --build --preset linux-debug
ctest --preset linux-debug
