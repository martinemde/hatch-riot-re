#!/usr/bin/env bash
# Read-only backup of a Hatch riot device before flashing anything.
#
#   scripts/backup.sh /dev/ttyUSB0
#
# What it does (never writes to the device):
#   1. chip-id / flash-id
#   2. eFuse summary -> stops if flash encryption or secure boot is enabled
#   3. reads the WHOLE flash twice and checks both reads are identical
#   4. prints the partition table from the backup
# Output goes to backups/<mac>_<timestamp>/.
set -euo pipefail

PORT="${1:?usage: $0 <serial-port> [baud]}"
BAUD="${2:-460800}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ESPTOOL="${ESPTOOL:-$ROOT/.venv-esphome/bin/esptool}"
ESPEFUSE="${ESPEFUSE:-$ROOT/.venv-esphome/bin/espefuse}"
PY="${PYTHON:-python3}"

for t in "$ESPTOOL" "$ESPEFUSE"; do
  [[ -x "$t" ]] || { echo "missing $t (create .venv-esphome, see esphome/README.md)"; exit 1; }
done

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
"$ESPTOOL" --port "$PORT" --baud "$BAUD" read-flash 0 ALL "$TMP/flash_verify.bin"

A="$(sha256sum "$OUT/flash_full.bin" | cut -d' ' -f1)"
B="$(sha256sum "$TMP/flash_verify.bin" | cut -d' ' -f1)"
if [[ "$A" != "$B" ]]; then
  echo "STOP: the two flash reads differ ($A vs $B). Check wiring/power and rerun."
  exit 3
fi
echo "$A  flash_full.bin" > "$OUT/flash_full.bin.sha256"
echo "Flash backup verified: $A"

echo "== partition table (from backup @0x8000)"
"$PY" -I - "$OUT/flash_full.bin" <<'EOF' | tee "$OUT/partitions.txt"
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

echo
echo "Backup complete: $OUT"
echo "Keep this directory safe. scripts/restore.sh can write it back."
