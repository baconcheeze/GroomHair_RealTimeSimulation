"""Fit joint-local convex body/arm pieces to actual Body / Body_2 skin.
Blender offline only. Runtime consumes ordinary Jolt convex hulls.
"""
import sys,json,struct,hashlib
from pathlib import Path
import numpy as np
R=Path(__file__).resolve().parents[1];A=R/'assets/moca';O=R/'artifacts/body-hulls-refined';O.mkdir(parents=True,exist_ok=True)
rig=json.loads((A/'rig.json').read_text());bones=rig['bones'];byname={b['name']:b['index'] for b in bones}
names=['Hips','Spine','Chest','Neck','Shoulder.L','Upper_arm.L','Lower_arm.L','Hand.L','Shoulder.R','Upper_arm.R','Lower_arm.R','Hand.R']
names += [b['name'] for b in bones if any(f in b['name'] for f in ('Thumb ','Index ','Middle ','Ring ','Little '))]
selected={byname[n]:n for n in names};stops={byname[n] for n in ['Head','Tail','Upper_leg.L','Upper_leg.R']}
owner=[];stopOwner=[]
for bone in bones:
 j=bone['index']
 while j>=0 and j not in selected and j not in stops:j=bones[j]['parent']
 owner.append(j if j in selected else -1)
 stopOwner.append(j if j in stops else -1)
# Small joint-overlap pieces cover skin blended between fixed convex owners.
bridges=[(byname['Head'],byname['Neck'],'Neck / head blend'),(byname['Upper_leg.L'],byname['Hips'],'Hip / left leg blend'),(byname['Upper_leg.R'],byname['Hips'],'Hip / right leg blend')]
for joint,_,name in bridges:selected[joint]=name
b=(A/'character.bin').read_bytes();nv=struct.unpack_from('<I',b,4)[0];p=np.frombuffer(b,dtype='<f4',offset=8,count=nv*3).reshape(-1,3);off=8+nv*12;nt=struct.unpack_from('<I',b,off)[0]
triangles=np.frombuffer(b,dtype='<u4',offset=off+4,count=nt*3).reshape(-1,3)
adjacency=[set() for _ in range(nv)]
for a,c,d in triangles:
 adjacency[a].update((int(c),int(d)));adjacency[c].update((int(a),int(d)));adjacency[d].update((int(a),int(c)))
off+=4+nt*12;nj=struct.unpack_from('<I',b,off)[0];off+=4
inv=np.frombuffer(b,dtype='<f4',offset=off,count=nj*16).reshape(nj,4,4).transpose(0,2,1).astype(np.float64);off+=nj*64;nw=struct.unpack_from('<I',b,off)[0];off+=4
w=np.frombuffer(b,dtype=np.dtype([('joint','<u4'),('weight','<f4')]),offset=off,count=nv*nw).reshape(nv,nw)
# Resolve original body mesh ranges; exclude loose clothing and sleeves.
vrm=next((R/'assets/imported/moca').glob('*.vrm')).read_bytes()
g=json.loads(vrm[20:20+struct.unpack_from('<I',vrm,12)[0]])
hidden=np.ones(nv,dtype=bool);base=0;skinRanges=[]
for node in g['nodes']:
 if 'mesh' not in node or node.get('name')=='Hair_Kemomimi':continue
 for primitive in g['meshes'][node['mesh']]['primitives']:
  if primitive['material']==1:continue
  count=g['accessors'][primitive['attributes']['POSITION']]['count']
  if node.get('name') in ('Body','Body_2'):
   hidden[base:base+count]=False;skinRanges.append({'name':node['name'],'start':base,'count':count})
  base+=count
clouds={j:[] for j in selected};sourceIds={j:[] for j in selected};sources={j:{'skin':0,'jacket':0} for j in selected}
def collect(points,joints,weights,label,mask=None):
 for i,point in enumerate(points):
  if mask is not None and mask[i]:continue
  sums={}
  for j,t in zip(joints[i],weights[i]):
   group=owner[int(j)]
   if t>0:sums[group]=sums.get(group,0)+float(t)
  group=max(sums,key=sums.get)
  # Include blend-border points in both owning pieces so an elbow/shoulder
  # blend does not open a hole between disjoint dominant-weight partitions.
  groups={g for g,t in sums.items() if g>=0 and t>=.2} if group>=0 else set()
  if group>=0:groups.add(group)
  for joint,parent,_ in bridges:
   stopWeight=sum(float(t) for j,t in zip(joints[i],weights[i]) if stopOwner[int(j)]==joint)
   if sums.get(parent,0)>=.15 and stopWeight>=.15:groups.add(joint)
  for g in groups:
   clouds[g].append(point.astype(np.float64));sourceIds[g].append((label,int(i)));sources[g][label]+=1
collect(p,w['joint'],w['weight'],'skin',hidden)
parts=[];records=[]
for joint,name in selected.items():
 pts=np.asarray(clouds[joint]);assert len(pts)>12,(name,len(pts))
 # Use small anatomically placed pieces, not a globally shrunken envelope.
 regions=[]
 if name in ('Hips','Spine','Chest'):
  mid=(pts[:,1].min()+pts[:,1].max())*.5
  for side,sideMask in [('left',pts[:,0]<=.001),('right',pts[:,0]>=-.001)]:
   for level,heightMask in [('lower',pts[:,1]<=mid+.001),('upper',pts[:,1]>=mid-.001)]:
    regions.append((side+' / '+level,sideMask&heightMask))
 elif name.startswith(('Upper_arm.','Lower_arm.')):
  axis=abs(pts[:,0]);mid=(axis.min()+axis.max())*.5
  regions=[('proximal',axis<=mid+.001),('distal',axis>=mid-.001)]
 else:regions=[('',np.ones(len(pts),dtype=bool))]
 for side,mask in regions:
  if side:
   # Include the actual surface triangles across the partition seam. Merely
   # slicing a sparse vertex cloud leaves a gap between consecutive skin rings.
   neighbours=set()
   for keep,(_,vertex) in zip(mask,sourceIds[joint]):
    if keep:neighbours.update(adjacency[vertex])
   mask=mask|np.array([vertex in neighbours for _,vertex in sourceIds[joint]])
  world=pts[mask]
  local=world@inv[joint,:3,:3].T+inv[joint,:3,3]
  # Supply original float32 source points to Jolt's own hull builder.
  # Do not use normals of numerically degenerate offline triangulations.
  hull=np.unique(local.astype('<f4'),axis=0).tolist()
  assert 4<=len(hull)<=65536,(name,len(hull))
  records.append({'name':name+(' / '+side if side else ''),'joint':joint,'sourcePoints':len(world),'inputPoints':len(hull),'sourceComposition':sources[joint],'sourceSkinVertices':[v for keep,(label,v) in zip(mask,sourceIds[joint]) if keep and label=='skin'],'bindBounds':[world.min(0).tolist(),world.max(0).tolist()]})
  parts.append((joint,hull))
with (A/'body-colliders.bin').open('wb') as f:
 f.write(struct.pack('<4sII',b'BCHL',2,len(parts)))
 for (joint,hull),record in zip(parts,records):
  f.write(struct.pack('<II',joint,len(hull)))
  for v in hull:f.write(struct.pack('<3f',*v))
  # Body/arm pieces follow the actual skin, so tightening them does not expose
  # blend-border vertices when the pose changes. Finger links stay bone-rigid.
  dynamic=record['sourceSkinVertices'] if not any(f in record['name'] for f in ('Thumb ','Index ','Middle ','Ring ','Little ')) else []
  f.write(struct.pack('<I',len(dynamic)))
  for vertex in dynamic:f.write(struct.pack('<I',vertex))
  record['skinnedSeamRefitVertices']=len(dynamic)
report={'version':2,'space':'Jolt right-handed metres, bone-local using stored inverse bind matrices; no reflection','method':'Body / Body_2 skin only. Exclude loose clothes and denim. Split torso by side and height, arms along length; 2mm partition overlap. Keep bone-weight overlap, fingers and live-skinned neck/hip seams. No global scaling.','skinRanges':skinRanges,'limits':'Convex approximation, not exact triangle collision. Clothing deliberately extends outside the body-sized collision surface.','sources':{n:hashlib.sha256((A/n).read_bytes()).hexdigest() for n in ['character.bin','rig.json']},'hulls':records,'sha256':hashlib.sha256((A/'body-colliders.bin').read_bytes()).hexdigest()}

(A/'body-colliders.json').write_text(json.dumps(report,indent=2));(O/'fit.json').write_text(json.dumps(report,indent=2));print(json.dumps(records,indent=2))
manifest=json.loads((A/'manifest.json').read_text())
for name in ('body-colliders.bin','body-colliders.json'):manifest['files'][name]=hashlib.sha256((A/name).read_bytes()).hexdigest()
(A/'manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
