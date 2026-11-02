"""Close the measured original posterior scalp boundary, preserving the rig.
Focused asset patch; does not regenerate or edit either existing hair groom.
"""
import collections,hashlib,json,math,shutil,struct
from pathlib import Path
R=Path(__file__).resolve().parents[1];A=R/'assets/moca';O=R/'artifacts/scalp';O.mkdir(parents=True,exist_ok=True)
backup=O/'source';backup.mkdir(exist_ok=True)
names=['character.bin','colors.bin','surface.bin']
reportpath=A/'scalp-patch.json'
old=json.loads(reportpath.read_text()) if reportpath.exists() else None
current=hashlib.sha256((A/'character.bin').read_bytes()).hexdigest()
if old and current==old['outputHashes']['character.bin']:
    source=backup
else:
    if old and current!=old['sourceHashes']['character.bin']:raise RuntimeError('Source changed: inspect before replacing the focused patch baseline')
    for n in names:
        if (backup/n).exists():
            assert (backup/n).read_bytes()==(A/n).read_bytes(),'Unexpected source change'
        else:shutil.copy2(A/n,backup/n)
    source=backup
b=(source/'character.bin').read_bytes();pos=0
def u():
    global pos
    v=struct.unpack_from('<I',b,pos)[0];pos+=4;return v
head=u();nv=u();verts=[struct.unpack_from('<3f',b,pos+i*12) for i in range(nv)];pos+=nv*12
nt=u();ts=[struct.unpack_from('<3I',b,pos+i*12) for i in range(nt)];pos+=nt*12
nj=u();binds=b[pos:pos+nj*64];pos+=nj*64
nw=u();weights=[struct.unpack_from('<'+'If'*nw,b,pos+i*nw*8) for i in range(nv)];pos+=nv*nw*8
tail=b[pos:] # All animation frames and collision bodies retained byte for byte.
cb=(source/'colors.bin').read_bytes();sb=(source/'surface.bin').read_bytes()
assert struct.unpack_from('<I',cb)[0]==nv and struct.unpack_from('<I',sb)[0]==nv
colors=[struct.unpack_from('<4f',cb,4+i*16) for i in range(nv)]
surfaces=[list(struct.unpack_from('<2fII4f4I4f',sb,4+i*64)) for i in range(nv)]
def key(p):return tuple(round(x*1e6) for x in p)
canonical={};representative={};mapping=[]
for i,p in enumerate(verts[:13320]):
    k=key(p)
    if k not in canonical:canonical[k]=len(canonical);representative[canonical[k]]=i
    mapping.append(canonical[k])
edges=collections.Counter();direct={}
for t in ts:
    if max(t)>=13320:continue
    t=[mapping[i] for i in t]
    if len(set(t))<3:continue
    for a,c in zip(t,t[1:]+t[:1]):edges[tuple(sorted((a,c)))]+=1;direct[tuple(sorted((a,c)))]=(a,c)
adj=collections.defaultdict(set)
for (a,c),n in edges.items():
    if n==1:adj[a].add(c);adj[c].add(a)
remaining=set(adj);candidates=[]
while remaining:
    todo=[remaining.pop()];comp=[]
    while todo:
        a=todo.pop();comp.append(a)
        for c in adj[a]:
            if c in remaining:remaining.remove(c);todo.append(c)
    if len(comp)==32 and min(verts[representative[i]][2] for i in comp)>.025:candidates.append(comp)
assert len(candidates)==1,'Expected exactly the diagnosed posterior boundary'
component=candidates[0];assert all(len(adj[i])==2 for i in component)
start=min(component);ordered=[start];prev=None;cur=start
while True:
    nex=min(i for i in adj[cur] if i!=prev)
    if nex==start:break
    ordered.append(nex);prev,cur=cur,nex
assert len(ordered)==32
if direct[tuple(sorted(ordered[:2]))]!=(ordered[0],ordered[1]):ordered=[ordered[0]]+ordered[:0:-1]
outer=[representative[i] for i in ordered];boundary=[verts[i] for i in outer]
def add(p):
    i=len(verts);verts.append(tuple(p));weights.append((head,1.,0,0.,0,0.,0,0.))
    # New scalp has a neutral skin material, not an undefined source atlas UV.
    colors.append((.98,.81,.84,1.));surfaces.append([0.,0.,0,0,0.,0.,1.,0.,head,0,0,0,1.,0.,0.,0.]);return i
# Concentric curved rings meet the exact imported edge vertices. Small rings
# converge to an occipital dome, never a flat fan or an overlaid sphere.
OCCIPITAL_INSET=.008 # Subtle 8 mm reduction; the original rim stays fixed.
apex=(0.,1.309,.108-OCCIPITAL_INSET);rings=12;last=outer;newts=[]
def dome(x,y):
    return .020+(.090-OCCIPITAL_INSET)*math.sqrt(max(.001,1-(x/.075)**2-((y-1.285)/.083)**2))
for ring in range(1,rings):
    r=1-ring/rings;inner=[]
    for p in boundary:
        x=r*p[0];y=apex[1]+r*(p[1]-apex[1])
        z=dome(x,y)+r**4*(p[2]-dome(p[0],p[1]))+(1-r*r)**2*(apex[2]-dome(apex[0],apex[1]))
        inner.append(add((x,y,z)))
    for i in range(32):
        j=(i+1)%32;newts.extend([(last[j],last[i],inner[i]),(last[j],inner[i],inner[j])])
    last=inner
tip=add(apex)
for i in range(32):newts.append((last[(i+1)%32],last[i],tip))
def cross(a,b):return (a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0])
normal=collections.defaultdict(lambda:[0.,0.,0.]);area=0.
for t in newts:
    a,c,d=[verts[i] for i in t];n=cross(tuple(c[k]-a[k] for k in range(3)),tuple(d[k]-a[k] for k in range(3)))
    length=math.sqrt(sum(x*x for x in n));assert length>1e-12
    area+=length/2
    for i in t:
        for k in range(3):normal[i][k]+=n[k]
assert sum(normal[i][2] for i in range(nv,len(verts)))>0,'Patch wound inward'
for i in range(nv,len(verts)):
    # Smooth display normals from the fitted support dome. Boundary correction
    # preserves the actual shared mesh edge; normals are not extra geometry.
    x,y,z=verts[i];eps=1e-5
    n=[-(dome(x+eps,y)-dome(x-eps,y))/(2*eps),-(dome(x,y+eps)-dome(x,y-eps))/(2*eps),1.]
    length=math.sqrt(sum(x*x for x in n));surfaces[i][4:7]=[x/length for x in n]
# Repair the atlas border only at the shared scalp rim, blending display
# normals there. Source positions/weights/topology remain untouched.
for i in outer:
    colors[i]=(.98,.81,.84,1.)
    n=normal[i];length=math.sqrt(sum(x*x for x in n))
    n=[n[k]/length+surfaces[i][4+k] for k in range(3)]
    length=math.sqrt(sum(x*x for x in n));surfaces[i][4:7]=[x/length for x in n]
for i,p in enumerate(verts[:nv]):
    if p[2]>.023:
        t=max(0,min(1,(p[1]-1.22)/.035));t=t*t*(3-2*t)
        colors[i]=(1-.02*t,1-.19*t,1-.16*t,t)
# Geometrically welded incidence must close EVERY old posterior edge and create
# no new boundary/non-manifold edge in the added patch.
counts=collections.Counter()
for t in ts+newts:
    ks=[key(verts[i]) for i in t]
    if len(set(ks))<3:continue
    for a,c in zip(ks,ks[1:]+ks[:1]):counts[tuple(sorted((a,c)))]+=1
for t in newts:
    ks=[key(verts[i]) for i in t]
    for a,c in zip(ks,ks[1:]+ks[:1]):assert counts[tuple(sorted((a,c)))]==2,'Open or nonmanifold patch edge'
ts+=newts
with (A/'character.bin').open('wb') as f:
    f.write(struct.pack('<II',head,len(verts)))
    for p in verts:f.write(struct.pack('<3f',*p))
    f.write(struct.pack('<I',len(ts)))
    for t in ts:f.write(struct.pack('<3I',*t))
    f.write(struct.pack('<I',nj));f.write(binds);f.write(struct.pack('<I',nw))
    for w in weights:f.write(struct.pack('<'+'If'*nw,*w))
    f.write(tail)
with (A/'colors.bin').open('wb') as f:
    f.write(struct.pack('<I',len(verts)))
    for c in colors:f.write(struct.pack('<4f',*c))
with (A/'surface.bin').open('wb') as f:
    f.write(struct.pack('<I',len(verts)))
    for s in surfaces:f.write(struct.pack('<2fII4f4I4f',*s))
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
report={'boundaryVertices':32,'addedVertices':len(verts)-nv,'addedTriangles':len(newts),'vertices':len(verts),'triangles':len(ts),'occipitalInsetMeters':OCCIPITAL_INSET,
        'patchAreaM2':area,'headJoint':head,'allPatchEdgesHaveTwoIncidentFaces':True,'originalVerticesTrianglesPreserved':True,
        'animationAndHullsUnchanged':True,'material':'neutral untextured skin','sourceHashes':{n:sha(source/n) for n in names},
        'outputHashes':{n:sha(A/n) for n in names},'hairHashes':{n:sha(A/n) for n in ('studio.hair','bob.hair')}}
reportpath.write_text(json.dumps(report,indent=2))
manifest=json.loads((A/'manifest.json').read_text(encoding='utf-8'));manifest['vertices']=len(verts);manifest['triangles']=len(ts)
manifest['scalpReinforcement']='32-vertex original posterior boundary closed with curved, head-skinned patch'
for n in names+['scalp-patch.json']:manifest['files'][n]=sha(A/n)
(A/'manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
(O/'topology.json').write_text(json.dumps(report,indent=2));print(json.dumps(report,indent=2))
