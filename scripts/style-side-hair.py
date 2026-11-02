"""Offline salon authoring: swept ear rim + tapered face framing, Jolt output.
Run with Blender. No simulation/reconstruction code is replaced.
"""
import sys,json,math,struct,hashlib
from pathlib import Path
import numpy as np
from mathutils import Vector
from mathutils.bvhtree import BVHTree
R=Path(__file__).resolve().parents[1];sys.path.insert(0,str(R/'scripts'))
from groom_surface import ScalpSurface
from ear_surface import EarEnvelope
from groom_drape import ScalpDrape,resample,smooth
A=R/'assets/moca';B=R/'authoring/side-style';O=R/'artifacts/hair-ear-fill';O.mkdir(parents=True,exist_ok=True)
surface=ScalpSurface(A/'character.bin')
def triangles(n):return n[2] if n[3] is None else triangles(n[2])+triangles(n[3])
bvh=BVHTree.FromPolygons(surface.v,triangles(surface.tree),all_triangles=True)
ears=[EarEnvelope(surface.v,s) for s in (-1,1)]
drape=ScalpDrape(surface,bvh,ears,A/'metahuman-wisps.shape')
blob=(B/'metahuman.hair').read_bytes();p=np.frombuffer(blob,dtype='<f4',offset=128).reshape(-1,32,3).copy()
m=np.frombuffer((B/'metahuman.materials').read_bytes(),dtype='<u4',offset=4).copy()
meta=json.loads((B/'metahuman.json').read_text())
def bezier(a,b,c,d,t):return a*(1-t)**3+3*b*(1-t)**2*t+3*c*(1-t)*t*t+d*t**3
def shell(theta,phi,offset):
 return drape.center+drape.direction(theta,phi)*(drape.radius(theta,phi)+offset)
def root(theta,phi):return drape.skull_bvh.find_nearest(shell(theta,phi,0))[0]
def contact(points):
 points=resample(points)
 for j in range(1,32):points[j]=drape.protect(points[j],.0012*smooth(0,.15,j/31))
 for _ in range(8):
  changed=False
  for j in range(31):
   for ear in ears:
    a,b,hit=ear.detour_segment(points[j],points[j+1],j==0,.0012)
    if hit:points[j],points[j+1]=a,b;changed=True
  if not changed:break
 return [tuple(v) for v in points]
def framing(side,u,v):
 # One coherent tapering lock in FRONT of the ear; no fishhook/radial curls.
 # Its section narrows towards its tip instead of keeping uniform ribbon width.
 clump=min(4,int(u*5));local=u*5-clump
 phi=side*(1.06+.18*u);theta=1.18+.14*v
 start=root(theta,phi)
 end=Vector((side*(.061+.001*clump+.001*(local-.5)),1.230-.0035*clump+.001*v,.005+.0015*clump))
 b=start+Vector((side*.002,-.015,.001))
 c=Vector((side*.066,end.y+.014,-.004+.002*clump))
 return contact([bezier(start,b,c,end,j/31) for j in range(32)])
def sweep(side,u,v):
 # Multiple overlapping tapered sheets follow the temple, pass over the
 # auricle, and feed behind it into the gathered tail. No rectangular cutout.
 phi0=.75+.45*u;phi1=2.20+.25*u
 theta0=.85+.45*v
 result=[]
 for j in range(32):
  t=j/31;phi=side*(phi0+(phi1-phi0)*t)
  theta=bezier(theta0,1.28+.18*v,1.44+.03*v,1.88+.22*v,t)
  lift=(.0007+.0006*math.sin(math.pi*v))*(1-math.exp(-t/.05))
  # Very small coherent clump texture, not independent noisy flyaways.
  theta+=.006*math.sin(6*math.pi*u)*math.sin(math.pi*t)
  q=shell(theta,phi,lift)
  if j==0:q=root(theta,phi)
  result.append(q)
 return contact(result)
def ear_rim(side,u,v):
 # A lower connecting layer follows the helix instead of terminating above
 # it. Existing cap sweeps alone left a disconnected exposed side patch.
 start=root(1.40+.10*v,side*(1.12+.12*u))
 b=Vector((side*(.076+.001*v),1.272+.004*v,.009+.005*u))
 c=Vector((side*(.077+.001*v),1.263+.004*v,.044+.003*u))
 end=Vector((side*(.063+.003*v),1.236+.010*v,.060+.006*u))
 return contact([bezier(start,b,c,end,j/31) for j in range(32)])
def ear_curtain(side,u,v):
 # A narrow ear-front lock joins the long original framing to the rim.
 start=root(1.32+.16*v,side*(1.21+.10*u))
 b=start+Vector((side*.002,-.020,-.002))
 c=Vector((side*.069,1.241+.004*v,.003))
 end=Vector((side*(.066+.0007*math.sin(2*math.pi*u)),1.223+.003*u,.009+.001*v))
 return contact([bezier(start,b,c,end,j/31) for j in range(32)])
replaced={4:0}
for reg in (4,):
 ids=np.flatnonzero(m==reg)
 for k,i in enumerate(ids):
  side=-1 if p[i,0,0]<0 else 1
  u=((k*.6180339887498949)%1);v=((k*.4142135623730951)%1)
  p[i]=framing(side,u,v);replaced[reg]+=1
base=np.frombuffer(blob,dtype='<f4',offset=128).reshape(-1,32,3)
assert np.array_equal(p[m!=4],base[m!=4]),'Non-ear source geometry changed'
added=[]
for side in (-1,1):
 for i in range(2400):
  u=(i*.6180339887498949)%1;v=(i*.4142135623730951)%1
  added.append(sweep(side,u,v))
 for i in range(900):
  added.append(ear_rim(side,(i*.6180339887498949)%1,(i*.4142135623730951)%1))
 for i in range(480):
  added.append(ear_curtain(side,(i*.6180339887498949)%1,(i*.4142135623730951)%1))
print('Authored framing',replaced,'swept layers',len(added),flush=True)
p=np.concatenate((p,np.asarray(added,dtype='<f4')));m=np.r_[m,np.full(len(added),4,dtype='<u4')]
assert np.isfinite(p).all() and np.linalg.norm(np.diff(p,axis=1),axis=2).min()>1e-7
header=bytearray(blob[:128]);struct.pack_into('<II',header,4,len(p),len(p)*32)
(A/'metahuman.hair').write_bytes(header+p.astype('<f4').tobytes())
(A/'metahuman.materials').write_bytes(struct.pack('<I',len(m))+m.astype('<u4').tobytes())
meta.update(profile='ear-connecting-layers-v12',name='Original face framing with connected ear layers',renderStrands=len(p),vertices=len(p)*32,
 newlyAuthoredStrands=len(added),redesignedSourceStrands=sum(replaced.values()),
 bounds=[p.min(axis=(0,1)).tolist(),p.max(axis=(0,1)).tolist()],materialRegionCounts={str(int(k)):int((m==k).sum()) for k in np.unique(m)})
meta['groups'].append({'sourceObject':'HairLab authored swept side layers','region':4,'firstStrand':len(p)-len(added),'sourceStrandCount':0,'strandCount':len(added),'authored':True,'trimmedStrands':0})
meta['sideDesign']={'method':'Authored ear sweeps and short ear-front wisps only; original fitted region 3 restored bitwise','replacedByRegion':replaced,'addedSweeps':len(added),'baseSha256':hashlib.sha256(blob).hexdigest(),'nonEarBasePositionsBitwiseEqual':True,'source':'authoring/side-style; original purchased ABC unchanged','units':'metres; native +Y up, -Z forward'}
meta['protectedFeatures']=['original fitted centre bangs and 761 face-framing curves','long twin tails','posterior/nape coverage'];meta['guides']='Authored ear layers simulate directly using existing Jolt region 4; all other source regions unchanged from fitted v9.'
meta['stagedSha256']={n:hashlib.sha256((A/n).read_bytes()).hexdigest() for n in ('metahuman.hair','metahuman.materials','metahuman.ears','metahuman.skull')}
(A/'metahuman.json').write_text(json.dumps(meta,indent=2));(O/'design.json').write_text(json.dumps(meta,indent=2))
manifest=json.loads((A/'manifest.json').read_text(encoding='utf-8'))
for n in ('metahuman.hair','metahuman.materials','metahuman.json'):manifest['files'][n]=hashlib.sha256((A/n).read_bytes()).hexdigest()
(A/'manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
