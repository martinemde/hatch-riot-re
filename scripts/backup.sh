#!/usr/bin/env bash
# Read-only backup of a Hatch riot device before flashing anything.
#
#   scripts/backup.sh <serial-port> [baud]                      full backup
#   scripts/backup.sh --verify-existing <backup-dir> <port> [baud]
#
# Full backup (never writes to the device):
#   1. chip-id / flash-id
#   2. eFuse summary -> stops if flash encryption or secure boot is enabled
#   3. reads the WHOLE flash twice and checks both reads are identical
#   4. prints the partition table from the backup
# Output goes to backups/<mac>_<timestamp>/.
#
# --verify-existing reads the flash ONCE more and compares it to an existing
# backups/<...>/flash_full.bin (e.g. when pass 2 of an earlier run failed on the
# long 16 MB read). Two independent reads agreeing is the same guarantee the full
# backup gives, without pulling the first image again. On a match it writes the
# flash_full.bin.sha256 that restore.sh checks. Both reads are kept on a mismatch.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ESPTOOL="${ESPTOOL:-$ROOT/.venv-esphome/bin/esptool}"
ESPEFUSE="${ESPEFUSE:-$ROOT/.venv-esphome/bin/espefuse}"
PY="${PYTHON:-python3}"

usage() {
  sed -n '2,13p' "$0" | sed 's/^# \{0,1\}//'
  exit 1
}

print_partitions() {  # $1 = dir containing flash_full.bin
  local dir="$1"
  echo "== partition table (from backup @0x8000)"
  "$PY" -I - "$dir/flash_full.bin" <<'EOF' | tee "$dir/partitions.txt"
import struct, sys
d = open(sys.argv[1], 'rb').read()[0x8000:0x8000 + 0xC00]
types = {0: 'app', 1: 'data'}
for i in range(0, len(d), 32):
    e = d[i:i + 32]
    if e[:2] != b'\xaa\x50':
        break
    t, st, off, size = e[2], e[3], *struct.unpack('<II', e[4:12])
    name = e[12:28].split(b'\0')[0].decode(errors='replace')
    print(f'{name:16} {types.get(t, t)!s:5} sub=0x{st:02x} offset=0x{off:06x} size=0x{size:06x}')
EOF
}

# Compare flash_full.bin vs flash_verify.bin in $1; finalize on match.
finalize() {
  local dir="$1" a b
  a="$(sha256sum "$dir/flash_full.bin" | cut -d' ' -f1)"
  b="$(sha256sum "$dir/flash_verify.bin" | cut -d' ' -f1)"
  if [[ "$a" != "$b" ]]; then
    echo "STOP: the two flash reads differ:"
    echo "  flash_full.bin   $a"
    echo "  flash_verify.bin $b"
    echo "The link is flaky; both reads are kept. Lower the baud and verify again"
    echo "until two reads agree:  $0 --verify-existing $dir <port> 115200"
    exit 3
  fi
  echo "$a  flash_full.bin" > "$dir/flash_full.bin.sha256"
  rm -f "$dir/flash_verify.bin"
  echo "Flash backup verified: $a"
  print_partitions "$dir"
  echo
  echo "Backup verified: $dir"
  echo "Keep this directory safe. scripts/restore.sh can write it back."
}

# Warn if the connected device is not the one this backup came from.
mac_check() {
  local dir="$1" devmac dirmac
  devmac="$("$ESPTOOL" --port "$PORT" --baud 115200 --after no-reset flash-id 2>/dev/null \
            | grep -m1 -oE 'MAC: *([0-9a-f]{2}:){5}[0-9a-f]{2}' | awk '{print $2}' | tr -d ':')"
  dirmac="$(basename "$dir" | cut -d_ -f1)"
  if [[ -n "$devmac" && -n "$dirmac" && "$devmac" != "$dirmac" ]]; then
    echo "WARNING: device MAC $devmac does not match backup dir ($dirmac)."
    echo "You may be verifying against a different unit's dump."
  fi
}

for t in "$ESPTOOL" "$ESPEFUSE"; do
  [[ -x "$t" ]] || { echo "missing $t (create .venv-esphome, see esphome/README.md)"; exit 1; }
done

# ---- verify-existing mode -------------------------------------------------
if [[ "${1:-}" == "--verify-existing" ]]; then
  DIR="${2:-}"; PORT="${3:-}"; BAUD="${4:-230400}"
  [[ -n "$DIR" && -n "$PORT" ]] || usage
  [[ -f "$DIR/flash_full.bin" ]] || { echo "no $DIR/flash_full.bin to verify against"; exit 1; }
  if [[ -f "$DIR/flash_full.bin.sha256" ]] && (cd "$DIR" && sha256sum -c flash_full.bin.sha256 >/dev/null 2>&1); then
    echo "Already verified: $DIR/flash_full.bin.sha256 checks out. Nothing to do."
    exit 0
  fi
  mac_check "$DIR"
  echo "== reading full flash (verify pass) at ${BAUD} baud"
  "$ESPTOOL" --port "$PORT" --baud "$BAUD" read-flash 0 ALL "$DIR/flash_verify.bin"
  finalize "$DIR"
  exit 0
fi

# ---- full backup mode -----------------------------------------------------
PORT="${1:-}"; BAUD="${2:-460800}"
[[ -n "$PORT" ]] || usage

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

echo "== chip-id / flash-id"
"$ESPTOOL" --port "$PORT" --baud 115200 --after no-reset flash-id | tee "$TMP/flash_id.txt"
MAC="$(grep -m1 -oE 'MAC: *([0-9a-f]{2}:){5}[0-9a-f]{2}' "$TMP/flash_id.txt" | awk '{print $2}' | tr -d ':')"
OUT="$ROOT/backups/${MAC:-unknown}_$(date +%Y%m%d-%H%M%S)"
mkdir -p "$OUT"
cp "$TMP/flash_id.txt" "$OUT/"

echo "== eFuse summary (read only)"
"$ESPEFUSE" --port "$PORT" --after no-reset summary | tee "$OUT/efuse_summary.txt"

"$PY" -I - "$OUT/efuse_summary.txt" <<'EOF'
import re, sys
text = open(sys.argv[1]).read()
def field(name):
    m = re.search(rf'^{name}\b.*?=\s*(\S+)', text, re.M)
    return m.group(1) if m else None
crypt = field('FLASH_CRYPT_CNT')
abs0, abs1 = field('ABS_DONE_0'), field('ABS_DONE_1')
print(f'FLASH_CRYPT_CNT={crypt} ABS_DONE_0={abs0} ABS_DONE_1={abs1}')
bad = []
try:
    if crypt is not None and bin(int(crypt, 0)).count('1') % 2 == 1:
        bad.append('flash encryption is ENABLED')
except ValueError:
    bad.append(f'could not parse FLASH_CRYPT_CNT ({crypt})')
for n, v in (('ABS_DONE_0 (secure boot v1)', abs0), ('ABS_DONE_1 (secure boot v2)', abs1)):
    if v not in (None, 'False', '0'):
        bad.append(f'{n} is set ({v})')
if crypt is None:
    bad.append('FLASH_CRYPT_CNT not found in summary')
if bad:
    print('STOP: ' + '; '.join(bad))
    print('Do not flash a replacement firmware until this is understood.')
    sys.exit(2)
print('eFuses OK: no flash encryption, no secure boot.')
EOF

echo "== reading full flash (pass 1)"
"$ESPTOOL" --port "$PORT" --baud "$BAUD" --after no-reset read-flash 0 ALL "$OUT/flash_full.bin"
echo "== reading full flash (pass 2, verification)"
# Written into the backup dir (not a temp dir), so a failed/interrupted pass 2
# leaves pass 1 recoverable via --verify-existing.
"$ESPTOOL" --port "$PORT" --baud "$BAUD" read-flash 0 ALL "$OUT/flash_verify.bin"

finalize "$OUT"
