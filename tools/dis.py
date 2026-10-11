"""Disassemble IROM/IRAM of an ESP32 app image into an annotated listing.

usage: dis.py IMAGE.bin OUT/listing.txt
Functions are found by scanning 4-byte-aligned `entry a1,N` opcodes. l32r literals
are resolved and annotated with the C string they point to. Capstone 6 does not
decode beqz.n/bnez.n/nop.n, so those are decoded by hand here.
"""
import struct, sys, pickle, re
import capstone as cs
img=open(sys.argv[1],'rb').read(); out=sys.argv[2]
nseg=img[1]; off=24; segs=[]
for i in range(nseg):
    la,ln=struct.unpack('<II',img[off:off+8]); segs.append((la,img[off+8:off+8+ln])); off+=8+ln
def rd(a,n):
    for la,d in segs:
        if la<=a and a+n<=la+len(d): return d[a-la:a-la+n]
    return None
def u32(a):
    b=rd(a,4); return struct.unpack('<I',b)[0] if b else None
def cstr(a):
    for la,d in segs:
        if la<=a<la+len(d) and la<0x40000000:
            e=d.find(b'\0',a-la)
            if e<0 or e-(a-la)>300: return None
            s=d[a-la:e]
            if len(s)>=3 and all(32<=c<127 or c in (9,10,13) for c in s): return s.decode()
    return None
code=[(la,d) for la,d in segs if 0x40080000<=la<0x40400000]
md=cs.Cs(cs.CS_ARCH_XTENSA,0); md.skipdata=False
# function starts: aligned 'entry a1,N'
starts=[]
for la,d in code:
    for o in range(0,len(d)-3,4):
        if d[o]==0x36 and (d[o+1]&0x0f)==1: starts.append(la+o)
starts.sort(); print('funcs',len(starts))
funcs={}
for i,s in enumerate(starts):
    end=starts[i+1] if i+1<len(starts) else s+0x4000
    la,d=[(la,d) for la,d in code if la<=s<la+len(d)][0]
    end=min(end,la+len(d))
    body=d[s-la:end-la]
    insns=[]
    o=0
    while o<len(body):
        r=list(md.disasm(body[o:o+4],s+o,count=1))
        if (not r or r[0].mnemonic.startswith('.byte')) and o+1<len(body):
            b0,b1=body[o],body[o+1]; op0=b0&0xf; t=b0>>4; rr=b1>>4; ss=b1&0xf
            fb=None
            if op0==0xc and t&0x8:
                imm=((t&3)<<4)|rr; fb=('bnez.n' if t&4 else 'beqz.n', f'a{ss}, . {s+o+4+imm:#x}')
            elif op0==0xd and rr==0xf:
                fb={0:('ret.n',''),1:('retw.n',''),2:('break.n',hex(ss)),3:('nop.n',''),6:('ill.n','')}.get(t)
            if fb:
                insns.append((s+o,fb[0],fb[1],2)); o+=2; continue
        if not r or r[0].mnemonic.startswith('.byte'):
            insns.append((s+o,'.byte',hex(body[o]),1)); o+=1; continue
        ins=r[0]; insns.append((ins.address,ins.mnemonic,ins.op_str,ins.size)); o+=ins.size
    funcs[s]=insns
pickle.dump({'starts':starts,'funcs':funcs},open(out+'.pkl','wb'))
with open(out,'w') as f:
    for s in starts:
        f.write(f'\n==== FUNC {s:08x}\n')
        for a,m,o,sz in funcs[s]:
            ann=''
            if m=='l32r':
                mm=re.search(r'0x([0-9a-f]+)',o)
                if mm:
                    la_=int(mm.group(1),16); v=u32(la_)
                    if v is not None:
                        ann=f' ; ={v:#x}'; st=cstr(v)
                        if st: ann+=' "'+st.replace('\n','\\n')[:120]+'"'
            f.write(f'{a:08x}: {m} {o}{ann}\n')
