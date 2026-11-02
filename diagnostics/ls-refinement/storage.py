"""Object-graph storage measurement; outside numerical timing."""
import pathlib,json,sys
from collections import deque
from core import Residual
from frozen import fl
sys.path.insert(0,str(pathlib.Path(__file__).parent.parent/'qr-audit'));import audit

def deep(x):
 seen=set();q=deque([x]);total=0
 while q:
  a=q.popleft()
  if id(a) in seen:continue
  seen.add(id(a));total+=sys.getsizeof(a)
  if isinstance(a,(list,tuple)):q.extend(a)
 return total
out={};source=pathlib.Path(sys.argv[1])
for name in ['primary__original_initial','synthetic-2-pins0__original_initial','synthetic-2-pins1__original_initial']:
 p=audit.decode(source/'frozen'/(name+'.json'));n=p['n'];a=audit.chunks(fl(p['A']),n);m=audit.chunks(fl(p['M']),n);r=Residual(a,fl(p['b']),m,fl(p['c']),n);out[name]={'PythonDecimalLSRowsAndB_deepBytes':deep([r.a,r.b]),'PythonDecimalNormalRowsAndC_deepBytes':deep([r.m,r.c]),'PythonDenseAInput_deepBytes':deep(a),'PythonDenseMInput_deepBytes':deep(m),'denseJNativePayloadBytes':p['rows']*n*8,'note':'sys.getsizeof graph excluding module/allocator overhead and IPC buffers'}
pathlib.Path(sys.argv[2]).write_text(json.dumps(out,indent=2))
