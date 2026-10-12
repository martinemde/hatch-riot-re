"""Convert an ESP32 app image (.bin) into an ELF32/Xtensa file for decompilers (Ghidra, REA).

usage: img2elf.py IMAGE.bin OUT.elf [listing.txt.xr] [--rom ESP_ROM_LD_DIR]
Each load segment becomes one PT_LOAD program header plus one section at its load address. If an .xr index
(from xr.py) is given, functions with an auto-name become FUNC symbols (name_ADDR when a name repeats).
--rom takes ESP-IDF's components/esp_rom/esp32/ld: the mask ROM (0x40000000-0x40070000) is added as an empty
NOBITS section carrying the PROVIDE()d ROM function names, so calls into ROM resolve to names.
"""
import collections
import glob
import pickle
import re
import struct
import sys

EM_XTENSA = 94
SHT_PROGBITS, SHT_SYMTAB, SHT_STRTAB, SHT_NOBITS = 1, 2, 3, 8
ROM_BASE, ROM_SIZE = 0x40000000, 0x70000

args = sys.argv[1:]
rom_dir = None
if '--rom' in args:
    i = args.index('--rom'); rom_dir = args[i + 1]; del args[i:i + 2]
sys.argv[1:] = args
SHF_WRITE, SHF_ALLOC, SHF_EXECINSTR = 1, 2, 4
PF_X, PF_W, PF_R = 1, 2, 4

d = open(sys.argv[1], 'rb').read()
_, nseg, _, _, entry = struct.unpack('<BBBBI', d[:8])
segs = []
off = 24
for i in range(nseg):
    la, ln = struct.unpack('<II', d[off:off + 8])
    segs.append((la, d[off + 8:off + 8 + ln]))
    off += 8 + ln


def kind(addr):
    """(section name, sh_flags, p_flags) from the ESP32 memory map."""
    if 0x400c2000 <= addr < 0x40c00000:
        return '.flash.text', SHF_ALLOC | SHF_EXECINSTR, PF_R | PF_X
    if 0x40070000 <= addr < 0x400c2000:
        return '.iram', SHF_ALLOC | SHF_EXECINSTR | SHF_WRITE, PF_R | PF_W | PF_X
    if 0x3f400000 <= addr < 0x3f800000:
        return '.flash.rodata', SHF_ALLOC, PF_R
    if 0x50000000 <= addr < 0x50002000:
        return '.rtc.slow', SHF_ALLOC | SHF_WRITE, PF_R | PF_W
    return '.dram', SHF_ALLOC | SHF_WRITE, PF_R | PF_W


# Symbols from the xr index.
syms = []
if len(sys.argv) > 3:
    X = pickle.load(open(sys.argv[3], 'rb'))
    picked = {f: collections.Counter(n).most_common(1)[0][0] for f, n in X['names'].items() if n}
    counts = collections.Counter(picked.values())
    for f, n in sorted(picked.items()):
        syms.append((f, n if counts[n] == 1 else f'{n}_{f:08x}'))

rom_syms = {}
if rom_dir:
    for ld in sorted(glob.glob(f'{rom_dir}/esp32.rom*.ld')):
        for name, addr in re.findall(r'PROVIDE\s*\(\s*(\w+)\s*=\s*(0x[0-9a-fA-F]+)\s*\)', open(ld).read()):
            a = int(addr, 16)
            if ROM_BASE <= a < ROM_BASE + ROM_SIZE:
                rom_syms.setdefault(a, name)

shstr = bytearray(b'\0')
def shname(s):
    o = len(shstr); shstr.extend(s.encode() + b'\0'); return o

strtab = bytearray(b'\0')
symtab = bytearray(16)  # null symbol
text_shndx = {}

nph = len(segs)
data_off = 52 + 32 * nph
body = bytearray()
phdrs, shdrs = [], [bytes(40)]
for i, (la, blob) in enumerate(segs):
    name, shf, pf = kind(la)
    foff = data_off + len(body)
    body += blob
    while len(body) % 4:
        body += b'\0'
    phdrs.append(struct.pack('<IIIIIIII', 1, foff, la, la, len(blob), len(blob), pf, 4))
    shdrs.append(struct.pack('<IIIIIIIIII', shname(f'{name}.{i}'), SHT_PROGBITS, shf, la, foff, len(blob), 0, 0, 4, 0))
    text_shndx[i] = len(shdrs) - 1

rom_shndx = None
if rom_syms:
    shdrs.append(struct.pack('<IIIIIIIIII', shname('.rom'), SHT_NOBITS, SHF_ALLOC | SHF_EXECINSTR, ROM_BASE,
                             data_off + len(body), ROM_SIZE, 0, 0, 4, 0))
    rom_shndx = len(shdrs) - 1
    syms += sorted(rom_syms.items())

def shndx_for(addr):
    if rom_shndx and ROM_BASE <= addr < ROM_BASE + ROM_SIZE:
        return rom_shndx
    for i, (la, blob) in enumerate(segs):
        if la <= addr < la + len(blob):
            return text_shndx[i]
    return None

for addr, name in syms:
    idx = shndx_for(addr)
    if idx is None:
        continue
    o = len(strtab); strtab.extend(name.encode() + b'\0')
    symtab += struct.pack('<IIIBBH', o, addr, 0, (1 << 4) | 2, 0, idx)  # GLOBAL FUNC

def add_section(name, stype, blob, link=0, info=0, entsize=0):
    global body
    foff = data_off + len(body)
    body += blob
    while len(body) % 4:
        body += b'\0'
    shdrs.append(struct.pack('<IIIIIIIIII', shname(name), stype, 0, 0, foff, len(blob), link, info, 4, entsize))
    return len(shdrs) - 1

strtab_idx = len(shdrs) + 1  # .symtab goes first, then .strtab
add_section('.symtab', SHT_SYMTAB, bytes(symtab), link=strtab_idx, info=1, entsize=16)
add_section('.strtab', SHT_STRTAB, bytes(strtab))
shstr_name = shname('.shstrtab')
shstr_off = data_off + len(body)
body += shstr
while len(body) % 4:
    body += b'\0'
shdrs.append(struct.pack('<IIIIIIIIII', shstr_name, SHT_STRTAB, 0, 0, shstr_off, len(shstr), 0, 0, 1, 0))

shoff = data_off + len(body)
ehdr = (b'\x7fELF' + bytes([1, 1, 1, 0]) + bytes(8)
        + struct.pack('<HHIIIIIHHHHHH', 2, EM_XTENSA, 1, entry, 52, shoff, 0x300, 52, 32, nph, 40, len(shdrs),
                      len(shdrs) - 1))
with open(sys.argv[2], 'wb') as f:
    f.write(ehdr + b''.join(phdrs) + body + b''.join(shdrs))
print(f'{sys.argv[2]}: {nseg} segments, entry {entry:#x}, {len(symtab) // 16 - 1} symbols')
