"""Build only sideburns on the approved v13 checkpoint; never edit it."""
from pathlib import Path
import sys,json,struct,math,hashlib
import numpy as np
from mathutils import Vector
from mathutils.bvhtree import BVHTree
R=Path(__file__).resolve().parents[1];sys.path.insert(0,str(R/'scripts'))
from groom_surface import ScalpSurface
from ear_surface import EarEnvelope
from groom_drape import ScalpDrape,smooth
B=R/'checkpoints/photo3-v13-approved/runnable/Assets/Moca';A=R/'assets/moca';O=R/'artifacts/sideburn-v14';O.mkdir(parents=True,exist_ok=True)
blob=(B/'metahuman.hair').read_bytes();baseline=np.frombuffer(blob,dtype='<f4',offset=128).reshape(-1,32,3)
p=baseline.copy();m=np.frombuffer((B/'metahuman.materials').read_bytes(),dtype='<u4',offset=4).copy()
meta=json.loads((B/'metahuman.json').read_text())
s=ScalpSurface(A/'character.bin')
def triangles(n):return n[2] if n[3] is None else triangles(n[2])+triangles(n[3])
bvh=BVHTree.FromPolygons(s.v,triangles(s.tree),all_triangles=True)
ears=[EarEnvelope(s.v,k,max_y=meta['earFit']['upperScalpExclusionMetres']) for k in (-1,1)]
d=ScalpDrape(s,bvh,ears,A/'metahuman-wisps.shape')
def strand(side,u,v,short=False):
 clump=min(2,int(u*3));local=u*3-clump
 theta=1.37+.10*v;phi=side*(1.15+.16*u)
 root=bvh.find_nearest(d.center+d.direction(theta,phi)*d.radius(theta,phi))[0]
 length_tier=.012 if short else 0
 end_y=1.224-.004*clump+length_tier+.006*(local**1.5-.5)
 end_z=.008+.003*clump+.0007*(local-.5)
 end=Vector((side*.064,end_y,end_z))
 b=Vector((side*.068,1.258+.002*v,-.008+.002*u))
 c=Vector((side*.067,end_y+.013,-.004+.001*clump))
 q=[root]
 for j in range(1,32):
  t=j/31
  point=root*(1-t)**3+3*b*(1-t)**2*t+3*c*(1-t)*t*t+end*t**3
  # Follow actual cheek/temple, retaining the yz silhouette. Do not snap
  # neighboring CVs to unrelated triangle normals or create a floating blade.
  hit=bvh.ray_cast(Vector((0,point.y,point.z)),Vector((side,0,0)),.2)[0]
  if hit is not None:point.x=hit.x+side*.0018*smooth(0,.15,t)
  nearest,n,_,_=bvh.find_nearest(point)
  if n.dot(nearest-d.center)<0:n=-n
  if (point-nearest).dot(n)<.0015:point=nearest+n*.0015
  q.append(point)
 for _ in range(8):
  changed=False
  for j in range(31):
   for ear in ears:
    a,b,repair=ear.detour_segment(q[j],q[j+1],j==0,.0012)
    if repair:q[j],q[j+1]=a,b;changed=True
  if not changed:break
 return [tuple(v) for v in q]
replaced=[]
added=[]
for side in (-1,1):
 for i in range(200):
  added.append(strand(side,(i*.6180339887498949)%1,(i*.4142135623730951)%1,short=i%4==0))
assert p.tobytes()==baseline.tobytes(),'Approved base hair changed'
q=np.concatenate((p,np.asarray(added,dtype='<f4')));new_m=np.r_[m,np.full(len(added),4,dtype='<u4')]
assert np.isfinite(q).all() and np.linalg.norm(np.diff(q,axis=1),axis=2).min()>1e-7
header=bytearray(blob[:128]);struct.pack_into('<II',header,4,len(q),len(q)*32)
(A/'metahuman.hair').write_bytes(header+q.astype('<f4').tobytes())
(A/'metahuman.materials').write_bytes(struct.pack('<I',len(new_m))+new_m.astype('<u4').tobytes())
meta.update(profile='photo3-sideburn-v14',name='Approved photo3 fit with feathered sideburns',renderStrands=len(q),vertices=len(q)*32,newlyAuthoredStrands=len(added),redesignedSourceStrands=len(replaced),materialRegionCounts={str(int(k)):int((new_m==k).sum()) for k in np.unique(new_m)},bounds=[q.min(axis=(0,1)).tolist(),q.max(axis=(0,1)).tolist()])
meta['groups'].append({'sourceObject':'HairLab feathered sideburns v14','region':4,'firstStrand':len(p),'sourceStrandCount':0,'strandCount':len(added),'authored':True,'trimmedStrands':0})
meta['sideburnDesign']={'approvedCheckpoint':'checkpoints/photo3-v13-approved','baseSha256':hashlib.sha256(blob).hexdigest(),'replacedSourceIds':replaced,'addedPerSide':200,'method':'Three tapered clumps and shorter underlayer on actual temple/cheek; original approved curls retained','unchangedBaseRegions':[0,1,2,3,5,6,7],'physicalSettingsChanged':False}
meta['stagedSha256']={n:hashlib.sha256((A/n).read_bytes()).hexdigest() for n in ('metahuman.hair','metahuman.materials','metahuman.ears','metahuman.skull')}
(A/'metahuman.json').write_text(json.dumps(meta,indent=2));(O/'design.json').write_text(json.dumps(meta,indent=2))
manifest=json.loads((A/'manifest.json').read_text(encoding='utf-8'))
for n in ('metahuman.hair','metahuman.materials','metahuman.json'):manifest['files'][n]=hashlib.sha256((A/n).read_bytes()).hexdigest()
(A/'manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
print('Sideburn-only update:',len(replaced),'replaced,',len(added),'added;',len(q),'render strands')
