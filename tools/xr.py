"""Build a call-graph / name index from a listing produced by dis.py.

usage: xr.py OUT/listing.txt   (writes OUT/listing.txt.xr)
Functions are auto-named by the identifier-like strings they reference
(usually __func__ from ESP_LOGx / ESP_RETURN_ON_FALSE).
"""
import re,sys,pickle,collections
src=sys.argv[1]
funcs=collections.OrderedDict(); cur=None
for l in open(src):
    if l.startswith('==== FUNC'): cur=int(l.split()[2],16); funcs[cur]=[]; continue
    if cur is None or not re.match(r'[0-9a-f]{8}: ',l): continue
    a,rest=l.split(':',1); funcs[cur].append((int(a,16),rest.strip()))
starts=sorted(funcs)
def tgt(a,ins):
    m=re.match(r'call(0|4|8|12) \. ([+-]0x[0-9a-f]+)',ins)
    if m: return ((a&~3)+int(m.group(2),16))
    m=re.match(r'l32r a8, .*; =0x(40[0-9a-f]{6})$',ins)
    return None
callees=collections.defaultdict(set); callers=collections.defaultdict(set)
for f,ins in funcs.items():
    pend=None
    for a,i in ins:
        t=tgt(a,i)
        m=re.match(r'l32r (a\d+), .*; =0x(40[0-9a-f]{6})$',i)
        if m: pend=(m.group(1),int(m.group(2),16))
        m2=re.match(r'callx(4|8|12) (a\d+)',i)
        if m2 and pend and pend[0]==m2.group(2): t=pend[1]
        if t: callees[f].add((a,t)); callers[t].add((f,a))
# names: identifier strings referenced
names=collections.defaultdict(list)
for f,ins in funcs.items():
    for a,i in ins:
        m=re.search(r'; =0x3f[0-9a-f]+ "([A-Za-z_][A-Za-z0-9_]{3,})"$',i)
        if m: names[f].append(m.group(1))
pickle.dump(dict(funcs=funcs,callers=callers,callees=callees,names=names),open(src+'.xr','wb'))
