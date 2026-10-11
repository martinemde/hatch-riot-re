"""Dump one function with call targets annotated by auto-name.

usage: nm.py listing.txt.xr FUNCADDR | dd.py
"""
import pickle,sys,re,collections
X=pickle.load(open(sys.argv[1],'rb'))
def nm(t):
    n=X['names'].get(t); return f'{t:08x}'+(f'<{collections.Counter(n).most_common(1)[0][0]}>' if n else '')
f=int(sys.argv[2],16)
for a,i in X['funcs'][f]:
    if i.startswith('.byte'): continue
    m=re.match(r'call(?:0|4|8|12) \. ([+-]0x[0-9a-f]+)',i)
    if m: i+='   -> '+nm((a&~3)+int(m.group(1),16))
    m=re.search(r'; =0x(40[0-9a-f]{6})$',i)
    if m and int(m.group(1),16) in X['funcs']: i+='   -> '+nm(int(m.group(1),16))
    print(f'{a:08x}: {i}')
