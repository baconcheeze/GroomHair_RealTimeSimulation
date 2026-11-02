"""Read-only VRM -> Jolt scene. Retains the imported rig; authors separate hair.
Python standard library only. Source face PSD is decoded separately by ffmpeg.
"""
import json, math, struct, hashlib, random
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'assets/moca'; OUT.mkdir(parents=True,exist_ok=True)
SOURCE=next((ROOT/'assets/imported/moca').glob('*.vrm'))
raw=SOURCE.read_bytes(); size=struct.unpack_from('<I',raw,12)[0]
g=json.loads(raw[20:20+size]); data=memoryview(raw)[28+size:]
def acc(i):
    a=g['accessors'][i]; b=g['bufferViews'][a['bufferView']]
    fmt={5126:'f',5125:'I',5123:'H',5121:'B'}[a['componentType']]
    n={'SCALAR':1,'VEC2':2,'VEC3':3,'VEC4':4,'MAT4':16}[a['type']]
    item=struct.Struct('<'+fmt*n); stride=b.get('byteStride',item.size)
    off=b.get('byteOffset',0)+a.get('byteOffset',0)
    assert not a.get('sparse') and not a.get('normalized')
    return [item.unpack_from(data,off+j*stride) for j in range(a['count'])]
def mul(a,b): return [[sum(a[i][k]*b[k][j] for k in range(4)) for j in range(4)] for i in range(4)]
def point(a,p,w=1):return tuple(sum(a[i][j]*p[j] for j in range(3))+w*a[i][3] for i in range(3))
def identity():return [[float(i==j) for j in range(4)] for i in range(4)]
def qm(a,b):
    x,y,z,w=a; X,Y,Z,W=b
    return (w*X+x*W+y*Z-z*Y,w*Y-x*Z+y*W+z*X,w*Z+x*Y-y*X+z*W,w*W-x*X-y*Y-z*Z)
def qaxis(axis,a):return tuple(v*math.sin(a/2) for v in axis)+(math.cos(a/2),)
def trs(n,q=None):
    x,y,z,w=q or n.get('rotation',[0,0,0,1]); m=identity()
    m[:3]=[[1-2*(y*y+z*z),2*(x*y-z*w),2*(x*z+y*w),0],
           [2*(x*y+z*w),1-2*(x*x+z*z),2*(y*z-x*w),0],
           [2*(x*z-y*w),2*(y*z+x*w),1-2*(x*x+y*y),0]]
    for i in range(3):
        m[i][3]=n.get('translation',[0,0,0])[i]
        for j in range(3): m[i][j]*=n.get('scale',[1,1,1])[j]
    return m
parents={c:i for i,n in enumerate(g['nodes']) for c in n.get('children',[])}
def poses(phase=0,animate=False):
    mats={}; quats={}
    def visit(i):
        if i in mats:return
        n=g['nodes'][i]; q=tuple(n.get('rotation',[0,0,0,1]))
        # Original imported head/neck hierarchy, original bind at phase zero.
        if i==45:q=qm(q,qaxis((0,1,0),.10*math.sin(phase)))
        if i==46:q=qm(q,qm(qaxis((0,1,0),.19*math.sin(phase)),qaxis((1,0,0),.055*math.sin(2*phase))))
        if animate and i in (193,213):q=qm(q,qaxis((0,0,1),1.13 if i==193 else -1.13))
        m=trs(n,q)
        if i in parents:
            p=parents[i];visit(p);m=mul(mats[p],m);q=qm(quats[p],q)
        mats[i]=m;quats[i]=q
    for i in range(len(g['nodes'])):visit(i)
    return mats,quats
bind,_=poses(); jointnodes=g['skins'][0]['joints']; ji={n:i for i,n in enumerate(jointnodes)}
ibms=acc(g['skins'][0]['inverseBindMatrices'])
skin_bind_error=max(abs(a-b) for skin in g['skins'] for row,canonical in zip(acc(skin['inverseBindMatrices']),ibms) for a,b in zip(row,canonical))
assert skin_bind_error<1e-6,skin_bind_error
# These exports differ by <=0.656 micrometres; Jolt's one rig uses the
# canonical Face inverse binds. Full original per-skin data remains in VRM.
ibms=[[[m[j*4+i] for j in range(4)] for i in range(4)] for m in ibms]
bind_error=max(abs(mul(bind[n],ibms[j])[a][b]-float(a==b)) for j,n in enumerate(jointnodes) for a in range(4) for b in range(4))
assert bind_error<1e-5,bind_error
verts=[];normals=[];uvs=[];weights=[];joints=[];triangles=[];textures=[];headtris=[];displayflags=[]
for ni,n in enumerate(g['nodes']):
    if 'mesh' not in n:continue
    # Hide only the animal-ear accessory; preserve the full original rig.
    if n.get('name')=='Hair_Kemomimi':continue
    skin=g['skins'][n['skin']]
    assert skin['joints']==jointnodes
    # Exported mesh nodes differ from identity only by sub-micrometre noise.
    assert max(abs(bind[ni][a][b]-float(a==b)) for a in range(4) for b in range(4))<1e-6
    for p in g['meshes'][n['mesh']]['primitives']:
        mat=p['material']
        # Face_Alpha is an expression overlay exported opaque by the VRM writer;
        # neutral display excludes its overlay faces, not character rig/geometry.
        if mat==1:continue
        a=p['attributes']; ps=acc(a['POSITION']); ns=acc(a['NORMAL']); us=acc(a['TEXCOORD_0'])
        js=acc(a['JOINTS_0']);ws=acc(a['WEIGHTS_0']);base=len(verts)
        verts.extend(ps);normals.extend(ns);uvs.extend(us);joints.extend(js);weights.extend(ws)
        textures.extend([{0:5,2:1,3:2,4:3,5:4}[mat]]*len(ps))
        displayflags.extend([1 if n.get('name') in ('Underwear_Bra','Hair_Tail') else 0]*len(ps))
        idx=[x[0] for x in acc(p['indices'])]
        for k in range(0,len(idx),3):
            t=tuple(base+idx[k+j] for j in range(3)); triangles.append(t)
            if mat==0 and min(verts[v][1] for v in t)>1.27:headtris.append(t)
assert all(len(v)==4 and abs(sum(v)-1)<1e-5 for v in weights)
pack=lambda fmt,*v:struct.pack('<'+fmt,*v)
def matbytes(m):return pack('16f',*(m[i][j] for j in range(4) for i in range(4)))
head=ji[46]; hulls=[]
# Actual imported cranial vertices, sampled deterministically, convexified by Jolt.
hp=[p for i,p in enumerate(verts[:13320]) if i%6==0 and p[1]>1.19]
hulls.append((head,hp))
for node,center,radius in [(45,(0,1.17,.025),(.031,.045,.030)),(38,(0,1.045,0),(.12,.105,.075))]:
    pts=[]
    for a in range(9):
        for b in range(16):
            th=math.pi*a/8;ph=2*math.pi*b/16
            pts.append((center[0]+radius[0]*math.sin(th)*math.cos(ph),center[1]+radius[1]*math.cos(th),center[2]+radius[2]*math.sin(th)*math.sin(ph)))
    hulls.append((ji[node],pts))
with (OUT/'character.bin').open('wb') as f:
    f.write(pack('II',head,len(verts)))
    for p in verts:f.write(pack('3f',*p))
    f.write(pack('I',len(triangles)))
    for t in triangles:f.write(pack('3I',*t))
    f.write(pack('I',len(jointnodes)))
    for m in ibms:f.write(matbytes(m))
    f.write(pack('I',4))
    for js,ws in zip(joints,weights):
        for j,w in zip(js,ws):f.write(pack('If',j,w))
    f.write(pack('I',240))
    for frame in range(240):
        ms,qs=poses(2*math.pi*frame/240,True)
        for node in jointnodes:
            q=qs[node];q=tuple(-v for v in q) if q[3]<0 else q
            f.write(pack('6f',*(ms[node][i][3] for i in range(3)),*q[:3]))
    f.write(pack('I',len(hulls)))
    for j,ps in hulls:
        f.write(pack('II',j,len(ps)))
        for p in ps:f.write(pack('3f',*p))
with (OUT/'colors.bin').open('wb') as f:
    f.write(pack('I',len(verts)));f.write(pack('4f',1,1,1,0)*len(verts))
with (OUT/'surface.bin').open('wb') as f:
    f.write(pack('I',len(verts)))
    for uv,tex,flags,n,js,ws in zip(uvs,textures,displayflags,normals,joints,weights):
        f.write(pack('2fII4f4I4f',*uv,tex,flags,*n,0,*js,*ws))
with (OUT/'binds.bin').open('wb') as f:
    f.write(pack('I',len(ibms)))
    for m in ibms:f.write(matbytes(m))
rig={'jointCount':len(jointnodes),'headJoint':head,'humanoid':g['extensions']['VRM']['humanoid'],
     'bones':[{'index':ji[n],'node':n,'name':g['nodes'][n].get('name',''),'parent':ji.get(parents.get(n),-1),'bindLocal':g['nodes'][n]} for n in jointnodes]}
(OUT/'rig.json').write_text(json.dumps(rig,ensure_ascii=False,indent=2),encoding='utf-8')
with (OUT/'skeleton.bin').open('wb') as f:
    f.write(pack('I',len(jointnodes)))
    for b in rig['bones']:f.write(pack('i',b['parent']))

# Raycast authored roots onto the actual imported upper-head triangles.
def sub(a,b):return tuple(a[i]-b[i] for i in range(3))
def cross(a,b):return (a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0])
def dot(a,b):return sum(x*y for x,y in zip(a,b))
def tree(ts):
    lo=tuple(min(verts[v][k] for t in ts for v in t) for k in range(3));hi=tuple(max(verts[v][k] for t in ts for v in t) for k in range(3))
    if len(ts)<12:return (lo,hi,ts,None)
    k=max(range(3),key=lambda j:hi[j]-lo[j]);ts.sort(key=lambda t:sum(verts[v][k] for v in t))
    mid=len(ts)//2;return (lo,hi,tree(ts[:mid]),tree(ts[mid:]))
bvh=tree(headtris)
center=(0,1.270,.022)
def ray(d):
    best=10.
    def visit(n):
        nonlocal best
        low,high,a,b=n;near=0.;far=best
        for k in range(3):
            if abs(d[k])<1e-14:
                if not low[k]<=center[k]<=high[k]:return
            else:
                v=(low[k]-center[k])/d[k];w=(high[k]-center[k])/d[k]
                near=max(near,min(v,w));far=min(far,max(v,w))
                if far<near:return
        if b is not None:visit(a);visit(b);return
        for t in a:
            p=verts[t[0]];e=sub(verts[t[1]],p);f=sub(verts[t[2]],p);h=cross(d,f);det=dot(e,h)
            if abs(det)<1e-14:continue
            s=sub(center,p);u=dot(s,h)/det
            if not 0<=u<=1:continue
            c=cross(s,e);v=dot(d,c)/det
            if v<0 or u+v>1:continue
            length=dot(f,c)/det
            if 0<length<best:best=length
    visit(bvh)
    if best>=1:
        # The imported facial surface has open seams. Find the nearest actual
        # upper-head triangle, retaining a real barycentric scalp attachment.
        target=tuple(center[k]+d[k]*.085 for k in range(3)); nearest=None;dist=1.
        def closest(n):
            nonlocal nearest,dist
            low,high,a,b=n
            if sum(max(low[k]-target[k],0,target[k]-high[k])**2 for k in range(3))>dist:return
            if b is not None:closest(a);closest(b);return
            for ids in a:
                vs=[verts[v] for v in ids];e=sub(vs[1],vs[0]);f=sub(vs[2],vs[0]);w=sub(target,vs[0])
                aa=dot(e,e);bb=dot(e,f);cc=dot(f,f);det=aa*cc-bb*bb;candidates=[]
                if det>1e-24:
                    u=(dot(w,e)*cc-dot(w,f)*bb)/det;v=(dot(w,f)*aa-dot(w,e)*bb)/det
                    if u>=0 and v>=0 and u+v<=1:candidates.append(tuple(vs[0][k]+u*e[k]+v*f[k] for k in range(3)))
                for l in range(3):
                    p=vs[l];edge=sub(vs[(l+1)%3],p);den=dot(edge,edge)
                    t=max(0,min(1,dot(sub(target,p),edge)/den)) if den>0 else 0
                    candidates.append(tuple(p[k]+t*edge[k] for k in range(3)))
                for p in candidates:
                    err=dot(sub(p,target),sub(p,target))
                    if err<dist:dist=err;nearest=p
        closest(bvh)
        assert nearest is not None
        return nearest
    return tuple(center[k]+d[k]*best for k in range(3))
rng=random.Random(20261101);points=[];count=16000;N=32
for i in range(count):
    fringe=i%5==0
    theta=.045+(.79 if fringe else 1.15)*rng.random()
    phi=(-1.05+2.10*rng.random()) if fringe else (.92+(2*math.pi-1.84)*rng.random())
    direction=(math.sin(theta)*math.sin(phi),math.cos(theta),-math.sin(theta)*math.cos(phi))
    root=ray(direction)
    # Measured radius along the imported scalp direction, not a sphere head.
    radius=math.sqrt(sum((root[k]-center[k])**2 for k in range(3)))
    for k in range(N):
        t=k/(N-1)
        if fringe:
            th=theta+(1.42+.15*abs(math.sin(phi*3))-theta)*t
            ph=phi+.055*math.sin(phi*3)*t*t
            p=(center[0]+.079*math.sin(th)*math.sin(ph),center[1]+.094*math.cos(th),center[2]-.087*math.sin(th)*math.cos(ph))
        else:
            cap=max(.001,(1.65-theta)*.09);drop=.205+.035*rng.random() if k==0 else drop
            distance=t*(cap+drop);th=min(1.65,theta+distance/.09)
            p=(center[0]+.079*math.sin(th)*math.sin(phi),center[1]+.094*math.cos(th)-max(0,distance-cap),center[2]-.087*math.sin(th)*math.cos(phi))
        if k>0:
            blend=min(1,t*10);p=tuple(root[j]+blend*(p[j]-root[j]) for j in range(3))
        points.append(root if k==0 else p)
header=bytearray(128);struct.pack_into('<4sIIII',header,0,b'HAIR',count,len(points),2,N-1)
with (OUT/'studio.hair').open('wb') as f:
    f.write(header)
    for p in points:f.write(pack('3f',*p))
with (OUT/'groom.materials').open('wb') as f:
    f.write(pack('I',count))
    for i in range(count):f.write(pack('I',int(i%5==0)))
manifest={'name':'Moca / project-authored midnight layered hair','sourceVrmSha256':hashlib.sha256(raw).hexdigest(),
 'units':'metres','space':'VRM/Jolt right-handed +Y up, character front -Z; reflection only at renderer boundary',
 'vertices':len(verts),'triangles':len(triangles),'bones':315,'headJoint':head,'frames':240,'animationHz':60,
 'bindIdentityMaxError':bind_error,'hulls':len(hulls),'authoredStrands':count,'pointsPerStrand':N,
 'sourceAnimationClips':0,'animation':'Project-authored head/neck loop on original hierarchy',
 'faceOverlay':'Neutral scene omits Face_Alpha expression overlay triangles; source retained in VRM',
 'hair':'Project-authored strand groom, roots raycast onto imported upper-head mesh; Jolt builds guides/render influences',
 'files':{str(p.relative_to(OUT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in OUT.rglob('*') if p.is_file() and p.name!='manifest.json'}}
(OUT/'manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps(manifest,ensure_ascii=True,indent=2))
