#!/bin/bash
# Launch the Serenity DE on Wayland via Lagom
# This starts WindowServerWayland, ClipboardServer, ConfigServer, and Terminal

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_DIR="${SCRIPT_DIR}/../../../.."
BUILD_DIR="${REPO_DIR}/Build/lagom"
BIN_DIR="${BUILD_DIR}/bin"

# Set up environment
export SERENITY_SOURCE_DIR="${REPO_DIR}"
export LD_LIBRARY_PATH="${BUILD_DIR}/lib:${LD_LIBRARY_PATH}"

# Create /res symlink if it doesn't exist (many Serenity apps use hardcoded /res paths)
if [ ! -e /res ] && [ -w / ]; then
    sudo ln -sf "${REPO_DIR}/Base/res" /res 2>/dev/null || true
fi
if [ ! -e /res ]; then
    echo "Warning: /res does not exist. Some icons may not load."
    echo "Run: sudo ln -sf ${REPO_DIR}/Base/res /res"
fi

# Clean up stale sockets
rm -f /tmp/portal/window /tmp/portal/clipboard /tmp/session/0/portal/config

# Ensure directories exist
mkdir -p /tmp/portal /tmp/session/0/portal

PIDS=()

# Trap to clean up background processes
cleanup() {
    echo "Cleaning up..."
    for pid in "${PIDS[@]}"; do
        kill "$pid" 2>/dev/null || true
    done
    rm -f /tmp/portal/window /tmp/portal/clipboard /tmp/session/0/portal/config
    wait 2>/dev/null
}
trap cleanup EXIT INT TERM

# Start ConfigServer
echo "Starting ConfigServer..."
"${BIN_DIR}/ConfigServerStandalone" &
PIDS+=($!)
sleep 0.2

# Start ClipboardServer
echo "Starting ClipboardServer..."
"${BIN_DIR}/ClipboardServer" &
PIDS+=($!)
sleep 0.2

# Start WindowServerWayland
echo "Starting WindowServerWayland..."
"${BIN_DIR}/WindowServerWayland" &
PIDS+=($!)
sleep 0.5

# Start Terminal (or whatever app is specified)
APP="${1:-Terminal}"
shift 2>/dev/null || true
echo "Starting ${APP}..."
"${BIN_DIR}/${APP}" "$@"
