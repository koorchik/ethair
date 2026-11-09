#!/usr/bin/env bash
# Builds and flashes the adapter firmware.
#   - If the adapter is running and reachable over its USB network
#     (http://192.168.73.1), the new image goes over the network (OTA).
#   - Otherwise it is flashed over USB serial (the board must show up as a
#     serial port: first flash, safe mode, or the ROM download mode).
# Usage: tools/flash.sh [cardputer|generic] [--usb]
set -euo pipefail
cd "$(dirname "$0")/.."

ENV_NAME="${1:-cardputer}"
FORCE_USB="${2:-}"
PIO="$(command -v pio || echo "$HOME/.platformio/penv/bin/pio")"
ADAPTER="http://192.168.73.1"

"$PIO" run -e "$ENV_NAME"
BIN=".pio/build/$ENV_NAME/firmware.bin"

if [[ "$FORCE_USB" != "--usb" ]] && curl -s -m 2 "$ADAPTER/api/status" >/dev/null; then
    echo "Adapter found at $ADAPTER, updating over the network..."
    curl -sS -m 180 -H 'X-BMCam: 1' -H 'Content-Type: application/octet-stream' --data-binary @"$BIN" "$ADAPTER/api/ota"
    echo "Waiting for the adapter to come back..."
    for _ in $(seq 1 30); do
        sleep 2
        if curl -s -m 2 "$ADAPTER/api/status"; then echo; exit 0; fi
    done
    echo "Adapter did not come back within 60 s" >&2
    exit 1
fi

echo "Flashing over USB serial..."
"$PIO" run -e "$ENV_NAME" -t upload
