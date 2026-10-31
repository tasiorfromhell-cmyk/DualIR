#!/usr/bin/env bash
# Rootless Debian 13 dependencies for the cloud/Linux validation build.
# APT verifies signed repository metadata and package checksums as usual.
set -euo pipefail

deps_root="${DUALIR_DEPS_ROOT:-/workspace/dualir-deps}"
if [[ "$deps_root" != /* || "$deps_root" == *'"'* || "$deps_root" == *"'"* || "$deps_root" == *$'\n'* ]]; then
    printf '%s\n' 'DUALIR_DEPS_ROOT must be an absolute path without quotes/newlines.' >&2
    exit 1
fi
if [[ "$(dpkg --print-architecture)" != amd64 ]]; then
    printf '%s\n' 'This cloud dependency helper supports Debian amd64.' >&2
    exit 1
fi
if ! rg -q '^VERSION_CODENAME=trixie$' /etc/os-release; then
    printf '%s\n' 'This cloud dependency helper requires Debian 13 (trixie).' >&2
    exit 1
fi

apt_root="$deps_root/apt"
sysroot="$deps_root/sysroot"
mkdir -p "$apt_root/lists/partial" "$apt_root/archives/partial" \
    "$apt_root/conf.d" "$apt_root/sources.list.d" "$sysroot" "$deps_root/cache"
touch "$apt_root/main.conf"
cat > "$apt_root/sources.list" <<'EOF'
deb [signed-by=/usr/share/keyrings/debian-archive-keyring.gpg] https://deb.debian.org/debian trixie main
deb [signed-by=/usr/share/keyrings/debian-archive-keyring.gpg] https://deb.debian.org/debian-security trixie-security main
EOF
# Avoid system APT hooks and locks: all writable APT state stays in deps_root.
cat > "$apt_root/apt.conf" <<EOF
Dir::Etc::parts "$apt_root/conf.d";
Dir::Etc::main "$apt_root/main.conf";
Dir::Etc::sourcelist "$apt_root/sources.list";
Dir::Etc::sourceparts "$apt_root/sources.list.d";
Dir::State::lists "$apt_root/lists";
Dir::State::status "$apt_root/status";
Dir::Cache::archives "$apt_root/archives";
APT::Sandbox::User "$(id -un)";
EOF
cp /var/lib/dpkg/status "$apt_root/status"
export APT_CONFIG="$apt_root/apt.conf"
/usr/bin/apt-get update
# Explicit runtime and metadata dependencies make the extracted prefix complete
# even when some of these packages are already installed on the host.
/usr/bin/apt-get --download-only --reinstall --no-install-recommends -y install \
    libasound2-dev libasound2t64 libbz2-dev libbz2-1.0 \
    libfreetype-dev libfreetype6 libfontconfig-dev libfontconfig1 \
    libpng-dev libpng16-16t64 libbrotli-dev libbrotli1 \
    libexpat1-dev libexpat1 zlib1g-dev zlib1g uuid-dev libuuid1 \
    libx11-dev libx11-6 libxext-dev libxext6 \
    libxinerama-dev libxinerama1 libxrandr-dev libxrandr2 \
    libxcursor-dev libxcursor1 libxcomposite-dev libxcomposite1 \
    libxfixes-dev libxfixes3 libxrender-dev libxrender1 \
    libxau-dev libxau6 libxdmcp-dev libxdmcp6 libxcb1-dev libxcb1 \
    x11proto-dev xtrans-dev xvfb xserver-common xauth fonts-dejavu-core
for archive in "$apt_root"/archives/*.deb; do
    dpkg-deb -x "$archive" "$sysroot"
done

cat > "$deps_root/env.sh" <<EOF
# Source before CMake configuration, build, or virtual-display checks.
export DUALIR_SYSROOT='$sysroot'
export PKG_CONFIG_SYSROOT_DIR='$sysroot'
export PKG_CONFIG_LIBDIR='$sysroot/usr/lib/x86_64-linux-gnu/pkgconfig:$sysroot/usr/share/pkgconfig'
export CMAKE_PREFIX_PATH='$sysroot/usr'
export CMAKE_INCLUDE_PATH='$sysroot/usr/include'
export CMAKE_LIBRARY_PATH='$sysroot/usr/lib/x86_64-linux-gnu'
export XDG_CACHE_HOME='$deps_root/cache'
export PATH='$sysroot/usr/bin':"\$PATH"
EOF
# This check fails if a development dependency or pkg-config metadata is missing.
PKG_CONFIG_SYSROOT_DIR="$sysroot" \
PKG_CONFIG_LIBDIR="$sysroot/usr/lib/x86_64-linux-gnu/pkgconfig:$sysroot/usr/share/pkgconfig" \
    pkg-config --exists alsa freetype2 fontconfig x11 xext xinerama xrandr xcursor xcomposite
printf 'Linux dependencies prepared. Run: source %q\n' "$deps_root/env.sh"
