"""Fit the purchased groom to reference photo 3, not additive invented panels.
Offline Blender authoring; runtime physics/reconstruction remain Jolt.
"""
import sys,json,math,struct,hashlib,bmesh
from pathlib import Path
import numpy as np
from mathutils import Vector
from mathutils.bvhtree import BVHTree
R=Path(__file__).resolve().parents[1];sys.path.insert(0,str(R/'scripts'))
from groom_surface import ScalpSurface
from groom_drape import ScalpDrape,smooth
from ear_surface import EarEnvelope
A=R/'assets/moca';B=R/'authoring/side-style';O=R/'artifacts/metahuman3-match';O.mkdir(parents=True,exist_ok=True)
s=ScalpSurface(A/'character.bin')
def triangles(n):return n[2] if n[3] is None else triangles(n[2])+triangles(n[3])
bvh=BVHTree.FromPolygons(s.v,triangles(s.tree),all_triangles=True)
# The broad ear envelope includes adjacent scalp up to 1.267m. The visible
# outer helix itself ends at ~1.255m; using the envelope top leaves a bald gap.
helix_top=max(v[1] for v in s.v[:13320] if abs(v[0])>.070 and .016<v[2]<.055 and 1.20<v[1]<1.28)
ears=[EarEnvelope(s.v,i,max_y=helix_top+.003) for i in (-1,1)];d=ScalpDrape(s,bvh,ears,A/'metahuman-wisps.shape')
blob=(B/'metahuman.hair').read_bytes();p=np.frombuffer(blob,dtype='<f4',offset=128).reshape(-1,32,3).copy()
m=np.frombuffer((B/'metahuman.materials').read_bytes(),dtype='<u4',offset=4).copy();base=p.copy()
tie_shift=Vector((0,-.020,.004))
tail_centres={}
for side in (-1,1):
 ids=np.flatnonzero(np.isin(m,[2,6])&(p[:,0,0]*side>0))
 old=Vector(p[ids,0].mean(axis=0));new=bvh.find_nearest(old+tie_shift)[0]
 tail_centres[side]=(old,new)
skull_blob=(A/'metahuman.skull').read_bytes();bm=bmesh.new()
vs=[bm.verts.new(v) for v in struct.iter_unpack('<3f',skull_blob[12:])]
bmesh.ops.convex_hull(bm,input=vs,use_existing_faces=False);bm.normal_update()
bm.verts.ensure_lookup_table();bm.verts.index_update()
skull_bvh=BVHTree.FromPolygons([v.co for v in bm.verts],[[v.index for v in f.verts] for f in bm.faces])
bm.free()
def physical_skull_contact(point):
 direction=(point-d.center).normalized()
 hit=skull_bvh.ray_cast(d.center,direction,.4)[0]
 if hit is None:raise ValueError('Missing physical skull intersection')
 distance=(hit-d.center).length
 return d.center+direction*max((point-d.center).length,distance+.0018)
def outer_ear_detour(ear,a,b,pin_a,prefer_above=False,prefer_back=False):
 if ear.segment_overlap(a,b,.0015) is None:return a,b
 choices=[]
 for n,offset in ear.planes:
  # The inner ear half-space lies inside the skull. Choosing that cheap
  # route alternates between penetrating the head and clearing the ear.
  if n.x*ear.side<-.10:continue
  da=max(0.,offset+.0015-n.dot(a));db=max(0.,offset+.0015-n.dot(b))
  if pin_a and da>1e-7:continue
  choices.append((da*da+db*db,da,db,n))
 if not choices:raise ValueError('No external ear route from scalp attachment')
 upper=[c for c in choices if c[3].y>.25]
 back=[c for c in choices if c[3].z>.25]
 eligible=back if prefer_back and back else upper if prefer_above and upper else choices
 _,da,db,n=min(eligible,key=lambda c:c[0])
 return a+n*da,b+n*db
def real_head_contact(q,convex=True,over_ear=False,behind_ear=False):
 # The smoothed skull field omits auricle triangles and has holes there.
 # Use the ACTUAL complete visible mesh to finish every moved connection.
 q=[v.copy() for v in q]
 q[0]=bvh.find_nearest(q[0])[0]
 for _ in range(4):
  for j in range(1,32):
   if q[j].y<1.185 or (q[j]-d.center).length>.18:continue
   hit,n,_,_=bvh.find_nearest(q[j])
   if n.dot(hit-d.center)<0:n=-n
   if (q[j]-hit).dot(n)<.0018:q[j]=hit+n*.0018
   if convex:q[j]=physical_skull_contact(q[j])
  for j in range(31):
   for ear in ears:
    q[j],q[j+1]=outer_ear_detour(ear,q[j],q[j+1],j==0,over_ear,behind_ear)
 return q
changed=0
for i in range(len(p)):
 reg=int(m[i])
 if reg in (0,5):
  q=[];weights=[]
  side=-1 if p[i,-1,0]<0 else 1
  old_c,new_c=tail_centres[side]
  target_end=bvh.find_nearest(new_c+(Vector(p[i,-1])-old_c)*.25)[0]
  end_shift=target_end-Vector(p[i,-1])
  nape_gather=p[i,0,1]<1.25 and p[i,0,2]>.045
  for v in p[i]:
   r,theta,phi=d.polar(Vector(v))
   az=smooth(.95,1.30,abs(phi))*(1-smooth(1.85,2.20,abs(phi)))
   w=az*smooth(.65,1.28,theta)*(1-smooth(1.65,2.05,theta))
   theta+=.46*w
   direction=d.direction(theta,phi)
   # Keep the layer close to the measured skull, preserving its own lift.
   nr=d.radius(theta,phi)+max(0.,r-d.radius(theta-.46*w,phi))
   point=d.center+direction*nr
   # Photo 3 exposes the whole helix. Lowering a cap without this measured
   # rim boundary instead produces a thick flap over the front half-ear.
   zone=smooth(.044,.056,abs(point.x))*smooth(-.025,.020,point.z)*(1-smooth(.049,.075,point.z))
   edge=helix_top+.002+.004*((point.z-.030)/.055)**2
   deficit=edge-point.y
   point.y+=zone*.5*(deficit+math.sqrt(deficit*deficit+.0002**2))
   # The reference gathers behind the upper ear, not above it. Carry the
   # cap's existing outgoing fibers into the same lowered tail attachment.
   point+=end_shift*smooth(.72,1.,len(q)/31)
   if nape_gather:
    # Nape fibers gather BEHIND the ear. Routing these through the same
    # lateral band as crown fibers makes an artificial flap below the lobe.
    rr,tt,pp=d.polar(point)
    z=(abs(pp)-2.12)/.03
    pp=math.copysign(2.12+.03*(max(z,0)+math.log1p(math.exp(-abs(z)))),pp)
    point=d.center+d.direction(tt,pp)*rr
   q.append(point);weights.append(w)
  # Follicles move ON the actual scalp, never into a free floating sheet.
  if weights[0]>1e-6:q[0]=d.skull_bvh.find_nearest(q[0])[0]
  for j in range(1,32):q[j]=d.protect(q[j],.0015)
  for _ in range(8):
   hit=False
   for j in range(31):
    for ear in ears:
     a,b,repair=ear.detour_segment(q[j],q[j+1],j==0,.0015)
     if repair:q[j],q[j+1]=a,b;hit=True
   if not hit:break
  p[i]=real_head_contact(q,over_ear=True);changed+=1
  if any(e.near(v) and e.separation(v)[0]<.018 for v in q for e in ears):m[i]=5
 elif reg in (2,6):
  side=-1 if p[i,0,0]<0 else 1
  old_c,new_c=tail_centres[side];shift=new_c-old_c
  q=[Vector(v)+shift for v in p[i]]
  root=bvh.find_nearest(new_c+(Vector(p[i,0])-old_c)*.25)[0]
  delta=root-q[0]
  arc=0
  for j in range(32):
   if j:arc+=(Vector(base[i,j])-Vector(base[i,j-1])).length
   q[j]+=delta*(1-smooth(0,.045,arc))
   # A low ponytail leaves the gather BEHIND the lobe before falling over
   # the shoulder. Otherwise contact projects the early stem sideways into
   # a conspicuous triangular flap in front of the ear.
   route=smooth(1.185,1.220,q[j].y)
   q[j].z+=max(0.,.055-q[j].z)*route
   if j:q[j]=d.protect(q[j],.0015)
  q=real_head_contact(q,behind_ear=True)
  for j in range(1,32):
   # Preserve the posterior styling route after convex contact as well.
   # A slanted ear plane can otherwise reintroduce forward z displacement.
   q[j].z+=max(0.,.055-q[j].z)*smooth(1.185,1.212,q[j].y)
  p[i]=q
 elif reg==4:
  # Two small reference-photo features: a fine ear-front curl and sparse
  # short sideburns. Do not snap each CV to a different triangle normal.
  side=-1 if p[i,0,0]<0 else 1
  u=(i*.6180339887498949)%1;v=(i*.4142135623730951)%1
  phi=side*(1.18+.08*u);theta=1.40+.08*v
  root=d.skull_bvh.find_nearest(d.center+d.direction(theta,phi)*d.radius(theta,phi))[0]
  if i%5:
   b=Vector((side*(.070+.001*u),1.260+.002*v,-.005))
   c=Vector((side*.071,1.245+.002*v,.008))
   end=Vector((side*(.069+.001*u),1.251+.002*v,.012))
  else:
   b=Vector((side*.071,1.255,-.010))
   c=Vector((side*.066,1.220+.003*v,-.005))
   end=Vector((side*(.058+.001*u),1.207+.004*v,0))
  q=[root]
  for j in range(1,32):
   t=j/31
   point=root*(1-t)**3+3*b*(1-t)**2*t+3*c*(1-t)*t*t+end*t**3
   hit=bvh.ray_cast(Vector((0,point.y,point.z)),Vector((side,0,0)),.20)[0]
   if hit is not None:point.x=hit.x+side*.0012
   q.append(d.protect(point,.0012))
  for _ in range(8):
   hit=False
   for j in range(31):
    for ear in ears:
     a,b,repair=ear.detour_segment(q[j],q[j+1],j==0,.0012)
     if repair:q[j],q[j+1]=a,b;hit=True
   if not hit:break
  p[i]=real_head_contact(q,convex=False)
assert np.isfinite(p).all() and np.linalg.norm(np.diff(p,axis=1),axis=2).min()>1e-7
original_m=np.frombuffer((B/'metahuman.materials').read_bytes(),dtype='<u4',offset=4)
for reg in (1,3,7):assert np.array_equal(p[original_m==reg],base[original_m==reg])
(A/'metahuman.hair').write_bytes(blob[:128]+p.astype('<f4').tobytes())
(A/'metahuman.materials').write_bytes(struct.pack('<I',len(m))+m.astype('<u4').tobytes())
with (A/'metahuman.ears').open('wb') as f:
 f.write(struct.pack('<4sII',b'EARH',1,len(ears)))
 for ear in ears:
  f.write(struct.pack('<I',len(ear.points)))
  for v in ear.points:f.write(struct.pack('<3f',*v))
meta=json.loads((B/'metahuman.json').read_text());ref=next(x for x in (R/'assets/MetaHuman').glob('*.jpg') if x.stem.endswith('3'))
meta['earFit']['geometry']=[ear.record() for ear in ears]
meta['earFit']['upperScalpExclusionMetres']=helix_top+.003
meta.update(profile='reference-photo3-v13',name='Reference photo 3 earline fit',materialRegionCounts={str(int(k)):int((m==k).sum()) for k in np.unique(m)},bounds=[p.min(axis=(0,1)).tolist(),p.max(axis=(0,1)).tolist()])
meta['referenceMatch']={'image':str(ref.relative_to(R)),'sha256':hashlib.sha256(ref.read_bytes()).hexdigest(),'method':'Existing side-cap follows measured outer helix and lowered compact gather, fine reference curl and sideburns; no additive panels','visibleHelixTopMetres':helix_top,'capEdgeClearanceMetres':.002,'tailTargetTranslationMetres':list(tie_shift),'rootGatherScale':.25,'tailCentres':{str(k):[list(v[0]),list(v[1])] for k,v in tail_centres.items()},'changedCapStrands':changed,'unchangedSourceRegions':[1,3,7],'baseSha256':hashlib.sha256(blob).hexdigest()}
meta['stagedSha256']={n:hashlib.sha256((A/n).read_bytes()).hexdigest() for n in ('metahuman.hair','metahuman.materials','metahuman.ears','metahuman.skull')}
(A/'metahuman.json').write_text(json.dumps(meta,indent=2));(O/'design.json').write_text(json.dumps(meta,indent=2))
manifest=json.loads((A/'manifest.json').read_text(encoding='utf-8'))
for n in ('metahuman.hair','metahuman.materials','metahuman.ears','metahuman.json'):manifest['files'][n]=hashlib.sha256((A/n).read_bytes()).hexdigest()
(A/'manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
print('Original strands',len(p),'locally lowered cap curves',changed,'reference',ref.name,flush=True)
