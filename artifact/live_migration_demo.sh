#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${MVVM_BUILD_DIR:-$ROOT/build}"
MEMORY_BYTES="${MVVM_DEMO_MEMORY_BYTES:-1048576}"
NUMA_NODE=""
DAX_FILE=""
PREFAULT="yes"
CONFIGURE_LOG="$(mktemp)"
trap 'rm -f "$CONFIGURE_LOG"' EXIT

usage() {
  cat <<'USAGE'
Usage: artifact/live_migration_demo.sh [options] [DAX_FILE]

Runs MVVM's live-migration checkpoint/restore demo.

Options:
  --build-dir DIR        CMake build directory (default: $MVVM_BUILD_DIR or ./build)
  --memory-bytes BYTES   WebAssembly linear-memory bytes to migrate (default: 1048576)
  --numa-node N          Bind the shared checkpoint mapping to an online NUMA/CXL System-RAM node
  --no-prefault          Skip NUMA pre-faulting
  -h, --help             Show this help

Without DAX_FILE or --numa-node, the demo uses memfd + MAP_SHARED. That proves
the live migration mechanics without claiming CXL hardware.
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --build-dir)
      BUILD_DIR="$2"
      shift 2
      ;;
    --memory-bytes)
      MEMORY_BYTES="$2"
      shift 2
      ;;
    --numa-node)
      NUMA_NODE="$2"
      shift 2
      ;;
    --no-prefault)
      PREFAULT="no"
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    --*)
      echo "unknown option: $1" >&2
      usage >&2
      exit 2
      ;;
    *)
      if [[ -n "$DAX_FILE" ]]; then
        echo "only one DAX_FILE may be provided" >&2
        usage >&2
        exit 2
      fi
      DAX_FILE="$1"
      shift
      ;;
  esac
done

if [[ -n "$DAX_FILE" && -n "$NUMA_NODE" ]]; then
  echo "DAX_FILE and --numa-node are mutually exclusive" >&2
  exit 2
fi

if ! cmake -S "$ROOT" -B "$BUILD_DIR" >"$CONFIGURE_LOG" 2>&1; then
  cat "$CONFIGURE_LOG" >&2
  exit 1
fi
cmake --build "$BUILD_DIR" --target mvvm_cxl_fork_stream_test -j"$(nproc)"

cmd=("$BUILD_DIR/mvvm_cxl_fork_stream_test" "--memory-bytes" "$MEMORY_BYTES")
if [[ -n "$DAX_FILE" ]]; then
  cmd+=("$DAX_FILE")
fi
if [[ -n "$NUMA_NODE" ]]; then
  cmd+=("--numa-node" "$NUMA_NODE")
  if [[ "$PREFAULT" == "no" ]]; then
    cmd+=("--no-prefault")
  fi
fi

echo
echo "MVVM live migration demo"
echo "root: $ROOT"
echo "build: $BUILD_DIR"
echo "command: ${cmd[*]}"
echo
"${cmd[@]}"
