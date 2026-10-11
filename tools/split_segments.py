"""Split an ESP32 app image (.bin) into its load segments.

usage: split_segments.py IMAGE.bin OUTDIR
Writes OUTDIR/seg<N>_<loadaddr>.bin and prints the segment table.
"""
import os
import struct
import sys

d = open(sys.argv[1], 'rb').read()
os.makedirs(sys.argv[2], exist_ok=True)
magic, nseg, mode, szf, entry = struct.unpack('<BBBBI', d[:8])
chip_id = struct.unpack('<H', d[12:14])[0]
print(f'segments={nseg} entry={entry:#x} chip_id={chip_id}')
off = 24
for i in range(nseg):
    la, ln = struct.unpack('<II', d[off:off + 8])
    print(f'{i} load={la:#010x} len={ln:#x} fileoff={off + 8:#x}')
    with open(f'{sys.argv[2]}/seg{i}_{la:08x}.bin', 'wb') as f:
        f.write(d[off + 8:off + 8 + ln])
    off += 8 + ln
print(f'end={off:#x} filelen={len(d):#x}')
