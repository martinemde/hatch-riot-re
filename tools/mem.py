"""Read 32-bit words from the image at a virtual address.

usage: mem.py IMAGE.bin ADDR:NBYTES [...]
"""
import struct,sys
img=open(sys.argv[1],'rb').read(); segs=[]; off=24
for i in range(img[1]):
    la,ln=struct.unpack('<II',img[off:off+8]); segs.append((la,img[off+8:off+8+ln])); off+=8+ln
def rd(a,n):
    for la,d in segs:
        if la<=a and a+n<=la+len(d): return d[a-la:a-la+n]
for spec in sys.argv[2:]:
    a,n=spec.split(':'); a=int(a,16); n=int(n,0)
    b=rd(a,n); print(f'{a:08x}:', ' '.join(f'{x:#x}' for x in struct.unpack(f'<{n//4}i',b)) if b else 'not in image (bss?)')
