#!/usr/bin/env bash
#
# Reproducibly build the pinned V8 as a static monolith on macOS (arm64 by
# default; set V8_TARGET_CPU=x64 for an Intel build). Mirrors
# tools/build_v8_linux.sh and stages the artifact into data/v8 for CMake.
#
# Usage:
#   tools/build_v8_macos.sh
#   V8_TARGET_CPU=x64 MACOSX_DEPLOYMENT_TARGET=11.0 tools/build_v8_macos.sh
#
# Requires Xcode Command Line Tools and Python 3 (for depot_tools/gclient).
#
set -euo pipefail

# --- Pinned inputs (keep in sync with cmake/v8_pin.cmake) -------------------
V8_VERSION="15.0.245.19"
V8_COMMIT="209c9cea0db17d8caf23e9d2c7de08c351609744"
DEPOT_TOOLS_COMMIT="${DEPOT_TOOLS_COMMIT:-}"

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WORK="${V8_WORK:-$HOME/iv8-v8-build-mac}"
DATA_DIR="$ROOT/data/v8"
DEPLOYMENT_TARGET="${MACOSX_DEPLOYMENT_TARGET:-11.0}"

HOST_ARCH="$(uname -m)"
TARGET_CPU="${V8_TARGET_CPU:-$HOST_ARCH}"
case "$TARGET_CPU" in
  arm64|x86_64) ;;
  *)
    echo "unsupported V8_TARGET_CPU='$TARGET_CPU' (expected arm64 or x86_64)" >&2
    exit 1
    ;;
esac
if [ "$TARGET_CPU" != "$HOST_ARCH" ]; then
  echo "WARNING: cross-building V8 for $TARGET_CPU on $HOST_ARCH."
  echo "         The extension wheel must be built for the same architecture."
fi

OUT_SUBDIR="out/${TARGET_CPU}.release.monolith"
mkdir -p "$WORK"
cd "$WORK"

export DEPOT_TOOLS_UPDATE=0          # never silently self-update
export DEPOT_TOOLS_METRICS=0
export PATH="$WORK/depot_tools:$PATH"

echo "==> [1/5] depot_tools"
if [ ! -d depot_tools ]; then
  git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git
fi
if [ -n "$DEPOT_TOOLS_COMMIT" ]; then
  git -C depot_tools checkout -q "$DEPOT_TOOLS_COMMIT"
fi
echo "    depot_tools commit: $(git -C depot_tools rev-parse HEAD)"
# DEPOT_TOOLS_UPDATE=0 suppresses the wrappers' auto-bootstrap; do it explicitly
# on a fresh checkout (same guard as tools/build_v8_manylinux.sh).
if [ ! -f depot_tools/python3_bin_reldir.txt ]; then
  "$WORK/depot_tools/ensure_bootstrap"
fi
if [ ! -f depot_tools/python3_bin_reldir.txt ]; then
  echo "depot_tools bootstrap failed: python3_bin_reldir.txt is missing" >&2
  exit 1
fi

echo "==> [2/5] fetch V8 source tree"
if [ ! -d v8 ]; then
  fetch --nohooks v8
fi
cd v8

echo "==> [3/5] checkout pinned V8 $V8_VERSION ($V8_COMMIT) and sync DEPS"
git fetch --tags origin
git checkout -q "$V8_COMMIT"
gclient sync -D

echo "==> [4/5] configure monolith build"
mkdir -p "$OUT_SUBDIR"
cat > "$OUT_SUBDIR/args.gn" <<EOF
is_debug = false
target_cpu = "$TARGET_CPU"
v8_monolithic = true
v8_monolithic_for_shared_library = true
v8_static_library = true
is_component_build = false
v8_use_external_startup_data = false
v8_enable_i18n_support = false
v8_enable_temporal_support = false
use_custom_libcxx = false
v8_enable_sandbox = false
# Do not let V8 hijack the process allocator: on macOS the allocator shim
# defines operator new/delete with visibility attributes that conflict with the
# system libc++ headers when use_custom_libcxx=false. V8 then uses the system
# allocator; PartitionAlloc remains available for the sandbox heap. This mirrors
# the Windows release args.
use_allocator_shim = false
use_partition_alloc_as_malloc = false
symbol_level = 1
mac_deployment_target = "$DEPLOYMENT_TARGET"
EOF

# V8's synced buildtools GN is preferred; fall back to a GN on PATH if the
# checkout does not ship the macOS binary.
GN="$WORK/v8/buildtools/mac/gn"
if [ ! -x "$GN" ]; then
  GN="$(command -v gn || true)"
fi
if [ -z "$GN" ]; then
  echo "gn not found (expected $WORK/v8/buildtools/mac/gn or gn on PATH)" >&2
  exit 1
fi
NINJA="$WORK/v8/third_party/ninja/ninja"
if [ ! -x "$NINJA" ]; then
  NINJA="$(command -v ninja || true)"
fi
if [ -z "$NINJA" ]; then
  echo "ninja not found (expected $WORK/v8/third_party/ninja/ninja or ninja on PATH)" >&2
  exit 1
fi

"$GN" gen "$OUT_SUBDIR"

echo "==> [5/5] build v8_monolith ($TARGET_CPU, deployment target $DEPLOYMENT_TARGET)"
"$NINJA" -C "$OUT_SUBDIR" v8_monolith

LIB="$WORK/v8/$OUT_SUBDIR/obj/libv8_monolith.a"
echo "-------------------------------------------------------------"
echo "V8 $V8_VERSION monolith built:"
ls -la "$LIB"
echo "size: $(du -h "$LIB" | cut -f1)"

echo "==> copying artifact + headers into $DATA_DIR"
rm -rf "$DATA_DIR"
mkdir -p "$DATA_DIR/lib" "$DATA_DIR/include" "$DATA_DIR/licenses"
cp "$LIB" "$DATA_DIR/lib/"
cp -R "$WORK/v8/include/." "$DATA_DIR/include/"
cp "$WORK/v8/LICENSE" "$DATA_DIR/licenses/LICENSE.v8" 2>/dev/null || true
for f in "$WORK"/v8/LICENSE.*; do
  [ -f "$f" ] && cp "$f" "$DATA_DIR/licenses/"
done
{
  echo "V8_VERSION=$V8_VERSION"
  echo "V8_COMMIT=$V8_COMMIT"
  echo "DEPOT_TOOLS_COMMIT=$(git -C "$WORK/depot_tools" rev-parse HEAD)"
  echo "GN_ARGS=monolithic,for_shared_library,static,i18n_off,temporal_off,use_custom_libcxx_false,sandbox_off,deployment_target_$DEPLOYMENT_TARGET"
  echo "PLATFORM=macos-$TARGET_CPU"
  echo "LIB_SIZE=$(du -h "$DATA_DIR/lib/libv8_monolith.a" | cut -f1)"
} > "$DATA_DIR/BUILD_INFO.txt"
echo "-------------------------------------------------------------"
cat "$DATA_DIR/BUILD_INFO.txt"
echo "-------------------------------------------------------------"
