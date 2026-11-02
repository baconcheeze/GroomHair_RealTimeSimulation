"""Fit the existing native Jolt head hull to the now-closed scalp surface.
Changes collision content only; retains mesh, skin, animation, other bodies.
"""
import hashlib,json,struct
from pathlib import Path
R=Path(__file__).resolve().parents[1];A=R/'assets/moca';p=A/'character.bin';b=p.read_bytes()
head,nv=struct.unpack_from('<II',b);v=[struct.unpack_from('<3f',b,8+i*12) for i in range(nv)]
nt=struct.unpack_from('<I',b,8+nv*12)[0];off=12+nv*12+nt*12
nj=struct.unpack_from('<I',b,off)[0];off+=4+nj*64
nw=struct.unpack_from('<I',b,off)[0];off+=4+nw*nv*8
nf=struct.unpack_from('<I',b,off)[0];off+=4+nf*nj*24
prefix=b[:off];nh=struct.unpack_from('<I',b,off)[0];off+=4
patch=json.loads((A/'scalp-patch.json').read_text());start=nv-patch['addedVertices']
# Include the repaired posterior dome, which was absent from the old open
# head's convex hull. Jolt retains ownership of hull construction and collision.
points=[pt for i,pt in enumerate(v[:13320]) if i%6==0 and pt[1]>1.19]+v[start:]
unique={tuple(round(x*1e7) for x in pt):pt for pt in points};points=list(unique.values())
payload=bytearray(struct.pack('<I',nh));changed=0
for i in range(nh):
    joint,n=struct.unpack_from('<II',b,off);off+=8;raw=b[off:off+n*12];off+=n*12
    if joint==head:
        payload.extend(struct.pack('<II',joint,len(points)))
        for pt in points:payload.extend(struct.pack('<3f',*pt))
        changed+=1
    else:payload.extend(struct.pack('<II',joint,n));payload.extend(raw)
assert off==len(b) and changed==1
p.write_bytes(prefix+payload)
digest=hashlib.sha256(p.read_bytes()).hexdigest()
patch['outputHashes']['character.bin']=digest
patch['animationAndHullsUnchanged']=False;patch['animationUnchanged']=True
patch['headHullRefit']={'points':len(points),'source':'Original head points plus all 353 repaired scalp vertices','otherBodiesUnchanged':True}
(A/'scalp-patch.json').write_text(json.dumps(patch,indent=2))
manifest=json.loads((A/'manifest.json').read_text(encoding='utf-8'))
for name in ('character.bin','scalp-patch.json'):manifest['files'][name]=hashlib.sha256((A/name).read_bytes()).hexdigest()
(A/'manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps(patch['headHullRefit']))
