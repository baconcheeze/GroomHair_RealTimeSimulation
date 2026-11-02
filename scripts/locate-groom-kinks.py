import bpy,numpy as np,json
from pathlib import Path
R=Path(__file__).resolve().parents[1]
p=np.frombuffer((R/'assets/moca/metahuman.hair').read_bytes(),dtype='<f4',offset=128).reshape(-1,32,3).astype(float)
m=np.frombuffer((R/'assets/moca/metahuman.materials').read_bytes(),dtype='<u4',offset=4)
e=np.diff(p,axis=1);l=np.linalg.norm(e,axis=2);a=np.arccos(np.clip(np.sum(e[:,:-1]*e[:,1:],axis=2)/(l[:,:-1]*l[:,1:]),-1,1))*180/np.pi
out={}
for region in sorted(set(m.tolist())):
    rows=np.flatnonzero(m==region);hits=np.argwhere(a[rows]>65)
    out[str(region)]={'sharpCount':len(hits),'byVertex':np.bincount(hits[:,1]+1,minlength=32).tolist() if len(hits) else [],'worst':[]}
    for row,col in sorted(hits,key=lambda x:a[rows[x[0]],x[1]],reverse=True)[:6]:
        strand=int(rows[row]);out[str(region)]['worst'].append({'strand':strand,'vertex':int(col+1),'angle':float(a[strand,col]),'position':p[strand,col+1].tolist()})
(R/'artifacts/hair-salon-finish/kink-locations.json').write_text(json.dumps(out,indent=2));print(json.dumps(out,indent=2))
