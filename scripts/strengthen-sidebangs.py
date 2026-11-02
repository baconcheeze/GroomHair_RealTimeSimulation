"""Fill side-bang roots/mid-lengths, preserve all existing hair and fine tips."""
from pathlib import Path
import sys,json,struct,hashlib
import numpy as np
from mathutils import Vector
from mathutils.bvhtree import BVHTree
R=Path(__file__).resolve().parents[1];sys.path.insert(0,str(R/'scripts'))
from groom_surface import ScalpSurface
A=R/'assets/moca';B=R/'authoring/sidebang-v15';O=R/'artifacts/sidebang-v15';O.mkdir(parents=True,exist_ok=True)
blob=(B/'metahuman.hair').read_bytes();base=np.frombuffer(blob,dtype='<f4',offset=128).reshape(-1,32,3)
m=np.frombuffer((B/'metahuman.materials').read_bytes(),dtype='<u4',offset=4)
meta=json.loads((B/'metahuman.json').read_text())
s=ScalpSurface(A/'character.bin')
def triangles(n):return n[2] if n[3] is None else triangles(n[2])+triangles(n[3])
bvh=BVHTree.FromPolygons(s.v,triangles(s.tree),all_triangles=True)
added=[];parents=[]
for side in (-1,1):
 ids=np.flatnonzero((m==3)&(base[:,20,0]*side>0));assert len(ids)>100
 curves=base[ids].astype(float)
 features=curves[:,[4,12,20,31]].reshape(len(ids),-1)
 for k in range(240):
  a=int((k*.6180339887498949%1)*len(ids))
  distances=np.sum((features-features[a])**2,axis=1)
  rms=np.sqrt(distances/4)
  separated=np.flatnonzero((rms>=.0015)&(rms<=.008))
  neighbors=separated[np.argsort(distances[separated])[:12]] if len(separated) else np.argsort(distances)[1:9]
  b=int(neighbors[k%len(neighbors)])
  blend=.35+.30*((k*.4142135623730951)%1)
  curve=curves[a]*(1-blend)+curves[b]*blend
  # New layers stop before the longest original ends, so extra density
  # fills the roots/middle without turning the jaw-length tips into a slab.
  arc=np.r_[0.,np.cumsum(np.linalg.norm(np.diff(curve,axis=0),axis=1))]
  fraction=.80+.18*((k*.7548776662466927)%1)
  samples=np.linspace(0,arc[-1]*fraction,32)
  q=np.stack([np.interp(samples,arc,curve[:,axis]) for axis in range(3)],axis=1)
  q[0]=bvh.find_nearest(Vector(q[0]))[0]
  # A sub-millimetre soft outer layer prevents new lines from being hidden
  # behind their parents by depth testing; roots and fine ends stay put.
  for j in range(1,31):
   hit,n,_,_=bvh.find_nearest(Vector(q[j]))
   if n.dot(hit-Vector(s.center))<0:n=-n
   q[j]+=np.array(n)*(.0007*np.sin(np.pi*j/31)**2)
  added.append(q);parents.append({'a':int(ids[a]),'b':int(ids[b]),'blend':blend,'lengthFraction':fraction})
p=np.concatenate((base,np.asarray(added,dtype='<f4')));materials=np.r_[m,np.full(len(added),3,dtype='<u4')]
assert np.isfinite(p).all() and np.linalg.norm(np.diff(p,axis=1),axis=2).min()>1e-7
assert p[:len(base)].tobytes()==base.tobytes()
header=bytearray(blob[:128]);struct.pack_into('<II',header,4,len(p),len(p)*32)
(A/'metahuman.hair').write_bytes(header+p.astype('<f4').tobytes())
(A/'metahuman.materials').write_bytes(struct.pack('<I',len(materials))+materials.astype('<u4').tobytes())
meta.update(profile='photo3-sidebang-v15',name='Photo3 fit with fuller tapered side bangs',renderStrands=len(p),vertices=len(p)*32,newlyAuthoredStrands=meta.get('newlyAuthoredStrands',0)+len(added),materialRegionCounts={str(int(k)):int((materials==k).sum()) for k in np.unique(materials)},bounds=[p.min(axis=(0,1)).tolist(),p.max(axis=(0,1)).tolist()])
meta['groups'].append({'sourceObject':'HairLab tapered side-bang infill v15','region':3,'firstStrand':len(base),'sourceStrandCount':0,'strandCount':len(added),'authored':True,'trimmedStrands':0})
meta['sideBangsDesign']={'baseSha256':hashlib.sha256(blob).hexdigest(),'existingBaseStrands':len(base),'existingBaseVerticesBitwiseEqual':True,'addedPerSide':240,'originalSideBangCount':int((m==3).sum()),'newLengthFractionRange':[.80,.98],'method':'Blend distinct neighboring original curves and stagger new layer ends; no physics change','displayRootMidCoverage':.464,'displayTipCoverage':.32,'sideburnsRemainHidden':True}
meta['stagedSha256']={n:hashlib.sha256((A/n).read_bytes()).hexdigest() for n in ('metahuman.hair','metahuman.materials','metahuman.ears','metahuman.skull')}
(A/'metahuman.json').write_text(json.dumps(meta,indent=2));(O/'design.json').write_text(json.dumps(meta,indent=2));(O/'parents.json').write_text(json.dumps(parents,indent=2))
manifest=json.loads((A/'manifest.json').read_text(encoding='utf-8'))
for n in ('metahuman.hair','metahuman.materials','metahuman.json'):manifest['files'][n]=hashlib.sha256((A/n).read_bytes()).hexdigest()
(A/'manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
print('Side bangs:',int((m==3).sum()),'->',int((materials==3).sum()),'; all existing',len(base),'strands unchanged.')
