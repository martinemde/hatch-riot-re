"""Query the .xr index.

usage: q.py listing.txt.xr byname  NAME      functions referencing string NAME
       q.py listing.txt.xr callers ADDR      call sites of ADDR
       q.py listing.txt.xr ctx     ADDR [N]  N insns before each call to ADDR
       q.py listing.txt.xr dump    ADDR      one function
"""
import pickle,sys,collections,re
X=pickle.load(open(sys.argv[1],'rb')); cmd=sys.argv[2]; arg=sys.argv[3]
F=X['funcs']
def nm(f): 
    n=X['names'].get(f); return f'{f:08x}'+(f'<{collections.Counter(n).most_common(1)[0][0]}>' if n else '')
if cmd=='byname':
    for f,n in X['names'].items():
        if arg in n: print(nm(f), 'callers:',len(X['callers'].get(f,())))
elif cmd=='callers':
    t=int(arg,16)
    for f,a in sorted(X['callers'].get(t,())): print(f'{a:08x} in {nm(f)}')
elif cmd=='ctx':
    t=int(arg,16); n=int(sys.argv[4]) if len(sys.argv)>4 else 25
    for f,a in sorted(X['callers'].get(t,())):
        ins=F[f]; idx=[k for k,(x,_) in enumerate(ins) if x==a][0]
        print(f'--- call at {a:08x} in {nm(f)}')
        for x,i in ins[max(0,idx-n):idx+1]:
            if not i.startswith('.byte'): print(f'  {x:08x}: {i}')
elif cmd=='dump':
    f=int(arg,16); print(nm(f))
    for x,i in F[f]:
        if not i.startswith('.byte'): print(f'  {x:08x}: {i}')
