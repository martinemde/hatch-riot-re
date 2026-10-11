#!/usr/bin/env bash
# Write a full-flash backup made by backup.sh back to the device (returns it to stock).
#
#   scripts/restore.sh /dev/ttyUSB0 backups/<mac>_<timestamp>
set -euo pipefail

PORT="${1:?usage: $0 <serial-port> <backup-dir>}"
DIR="${2:?usage: $0 <serial-port> <backup-dir>}"
BAUD="${3:-460800}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ESPTOOL="${ESPTOOL:-$ROOT/.venv-esphome/bin/esptool}"
IMG="$DIR/flash_full.bin"

[[ -f "$IMG" && -f "$IMG.sha256" ]] || { echo "missing $IMG or its .sha256"; exit 1; }
(cd "$DIR" && sha256sum -c flash_full.bin.sha256) || { echo "backup checksum mismatch, refusing"; exit 1; }

echo "== device"
"$ESPTOOL" --port "$PORT" --baud 115200 --after no-reset flash-id | tee /dev/stderr | grep -q 'MAC' \
  || { echo "no device"; exit 1; }
MAC_DEV="$("$ESPTOOL" --port "$PORT" --baud 115200 --after no-reset chip-id | grep -m1 -oE '([0-9a-f]{2}:){5}[0-9a-f]{2}' | tr -d ':')"
MAC_BAK="$(basename "$DIR" | cut -d_ -f1)"
if [[ "$MAC_DEV" != "$MAC_BAK" ]]; then
  echo "WARNING: device MAC $MAC_DEV does not match backup $MAC_BAK."
  echo "Restoring another unit's flash would clone its identity/certificates."
fi

read -r -p "Overwrite the ENTIRE flash of $MAC_DEV with $IMG? Type 'restore' to continue: " ans
[[ "$ans" == "restore" ]] || { echo "aborted"; exit 1; }

"$ESPTOOL" --port "$PORT" --baud "$BAUD" write-flash 0 "$IMG"
"$ESPTOOL" --port "$PORT" --baud "$BAUD" verify-flash 0 "$IMG"
echo "Restore complete and verified."
