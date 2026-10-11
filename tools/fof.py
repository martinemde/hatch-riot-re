"""Print the function containing each address.

usage: fof.py listing.txt.xr ADDR [ADDR...]
"""
import pickle,sys,bisect
X=pickle.load(open(sys.argv[1],'rb')); s=sorted(X['funcs'])
for a in sys.argv[2:]:
    a=int(a,16); print(f'{s[bisect.bisect_right(s,a)-1]:08x}')
