"""Original cropped indigo jacket, fitted in Moca bind space; stdlib only.
Independent display asset. Never modifies the character, rig, or Jolt hair.
"""
import math,struct,json,hashlib,collections
from pathlib import Path
R=Path(__file__).resolve().parents[1];A=R/'assets/moca'
raw=(A/'character.bin').read_bytes();nv=struct.unpack_from('<I',raw,4)[0]
nt=struct.unpack_from('<I',raw,8+nv*12)[0];off=12+nv*12+nt*12
nj=struct.unpack_from('<I',raw,off)[0];off+=4+nj*64
nw=struct.unpack_from('<I',raw,off)[0];off+=4;assert nw==4 and nj==315
rig=json.loads((A/'rig.json').read_text(encoding='utf-8'));bones={b['name']:b['index'] for b in rig['bones']}
skin_mode='torso';skin_sign=1
def blendweights(a,b,t):return (a,1-t,b,t,0,0.,0,0.)
def skin(p):
    if skin_mode=='torso':return blendweights(bones['Spine'],bones['Chest'],max(0,min(1,(p[1]-.970)/.055)))
    suffix='.R' if skin_sign>0 else '.L';upper=bones['Upper_arm'+suffix];lower=bones['Lower_arm'+suffix]
    if skin_mode=='cap':
        t=max(0,min(1,(abs(p[0])-.042)/.045));t=t*t*(3-2*t)
        return blendweights(bones['Chest'],upper,t)
    t=max(0,min(1,(abs(p[0])-.265)/.065));t=t*t*(3-2*t)
    return blendweights(upper,lower,t)
def sub(a,b):return tuple(a[k]-b[k] for k in range(3))
def add(a,b):return tuple(a[k]+b[k] for k in range(3))
def scale(a,s):return tuple(x*s for x in a)
def dot(a,b):return sum(x*y for x,y in zip(a,b))
def cross(a,b):return (a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0])
def unit(a):return scale(a,1/max(1e-12,math.sqrt(dot(a,a))))
V=[];N=[];UV=[];C=[];F=[];W=[];T=[];parts=[]
DENIM=(.23,.34,.49);PANEL=(.26,.38,.53);DARK=(.16,.25,.38);THREAD=(.73,.58,.35);BRASS=(.66,.48,.22)
def vertex(p,n,uv,color,flags):
    i=len(V);V.append(p);N.append(unit(n));UV.append(uv);C.append(color);F.append(flags);W.append(skin(p));return i
def patch(name,fn,nu,nv,color=DENIM,flags=0,outward=None,omit=None):
    start=len(T);ids=[]
    for j in range(nv+1):
        row=[]
        for i in range(nu+1):
            u=i/nu;v=j/nv;p=fn(u,v);e=.0001
            du=sub(fn(min(1,u+e),v),fn(max(0,u-e),v));dv=sub(fn(u,min(1,v+e)),fn(u,max(0,v-e)))
            n=unit(cross(du,dv))
            if outward:
                if dot(n,n)<.1:n=unit(outward(p))
                elif dot(n,outward(p))<0:n=scale(n,-1)
            row.append(vertex(p,n,(u,v),color,flags))
        ids.append(row)
    for j in range(nv):
        for i in range(nu):
            if omit and omit((i+.5)/nu,(j+.5)/nv):continue
            a,b,c,d=ids[j][i],ids[j][i+1],ids[j+1][i+1],ids[j+1][i]
            if dot(cross(sub(V[b],V[a]),sub(V[d],V[a])),N[a])>0:T.extend([(a,b,c),(a,c,d)])
            else:T.extend([(a,c,b),(a,d,c)])
    parts.append({'name':name,'triangles':len(T)-start})
def cylinder(name,a,b,r,color,flags=2,sides=6):
    axis=unit(sub(b,a));ref=(0,1,0) if abs(axis[1])<.9 else (1,0,0)
    x=unit(cross(axis,ref));y=cross(axis,x)
    patch(name,lambda u,v:add(add(a,scale(sub(b,a),v)),scale(add(scale(x,math.cos(u*math.tau)),scale(y,math.sin(u*math.tau))),r)),sides,1,color,flags)
def stitch(name,fn,count=35):
    for i in range(count):cylinder(name,fn((i+.12)/count),fn((i+.66)/count),.00033,THREAD)
def button(name,p,r=.0028):
    # Shallow domed brass disk facing the character front (-Z).
    patch(name,lambda u,v:(p[0]+r*v*math.cos(math.tau*u),p[1]+r*v*math.sin(math.tau*u),p[2]-.0012*(1-v*v)),12,3,BRASS,4,lambda p:(0,0,-1))
rows=[(.985,.108,.090),(1.003,.112,.116),(1.045,.125,.153),(1.085,.128,.146),(1.120,.118,.098),(1.146,.105,.064)]
backrows=[.068,.070,.079,.078,.068,.054]
def radii(y):
    for a,b in zip(rows,rows[1:]):
        if y<=b[0]:
            t=max(0,min(1,(y-a[0])/(b[0]-a[0])));return a[1]+t*(b[1]-a[1]),a[2]+t*(b[2]-a[2])
    return rows[-1][1:]
def edge(y):return .30+.22*max(0,min(1,(y-1.055)/.091))
def backradius(y):
    for i,(a,b) in enumerate(zip(rows,rows[1:])):
        if y<=b[0]:
            t=max(0,min(1,(y-a[0])/(b[0]-a[0])));return backrows[i]+t*(backrows[i+1]-backrows[i])
    return backrows[-1]
def torso(u,v,offset=0):
    y=.985+.161*v;rx,rz=radii(y);phi=edge(y)+(math.tau-2*edge(y))*u
    if math.cos(phi)<0:rz=backradius(y)
    return ((rx+offset)*math.sin(phi),y,.020-(rz+offset)*math.cos(phi))
out=lambda p:(p[0],0,p[2]-.020)
patch('tailored body panels',torso,64,28,DENIM,0,out)
patch('shoulder yoke',lambda u,v:((.035+.070*v)*math.sin(.52+(math.tau-1.04)*u),
      1.146+.016*(1-v),.020-(.038+.026*v)*math.cos(.52+(math.tau-1.04)*u)),64,8,DENIM,0,lambda p:(0,1,0))
patch('cropped waistband',lambda u,v:torso(u,v*.10,.0015),64,3,DARK,0,out)
for v in (.018,.082):stitch('waistband topstitch',lambda u,v=v:torso(u,v,.0023),115)
for side in (0,1):
    # Small folded front placket, tapered with the open V.
    patch('front placket',lambda u,v,side=side:torso((u*.012 if side==0 else 1-u*.012),v,.0017),2,28,PANEL,0,out)
    stitch('front seam',lambda v,side=side:torso(.008 if side==0 else .992,v,.003),55)
def frontxy(x,y,offset=.002):
    rx,rz=radii(y);return (x,y,.020-rz*math.sqrt(max(.02,1-(x/rx)**2))-offset)
for sign in (-1,1):
    skin_mode='torso';skin_sign=sign
    # Chest patch pocket plus a pointed fold-over flap.
    patch('chest pocket',lambda u,v,s=sign:frontxy(s*(.052+.040*u),1.039+.039*v,.0035),8,8,PANEL,0,lambda p:(0,0,-1))
    patch('pocket flap',lambda u,v,s=sign:frontxy(s*(.050+.044*u),1.084-v*(.011+.005*(1-abs(u*2-1))),.005),8,3,DARK,0,lambda p:(0,0,-1))
    for x in (.054,.090):stitch('pocket side stitch',lambda v,s=sign,x=x:frontxy(s*x,1.041+.034*v,.0045),12)
    stitch('pocket bottom stitch',lambda u,s=sign:frontxy(s*(.054+.036*u),1.041,.0045),14)
    button('pocket brass snap',frontxy(sign*.072,1.077,.006))
    # Folded collar wings with deliberately broad, jacket-like points.
    corners=[(sign*.024,1.166,-.014),(sign*.074,1.143,-.025),(sign*.070,1.103,-.070),(sign*.040,1.131,-.057)]
    def collar(u,v,c=corners):return tuple((1-v)*((1-u)*c[0][k]+u*c[1][k])+v*((1-u)*c[3][k]+u*c[2][k]) for k in range(3))
    patch('folded collar',collar,8,8,PANEL,0,lambda p:(0,.5,-1))
    for u in (.035,.965):stitch('collar stitch',lambda v,u=u,fn=collar:add(fn(u,v),(0,.0005,-.0006)),20)
    # Full sleeves in the original T bind pose; existing skin drives the drop.
    def sleeve(u,v,s=sign,extra=0):
        x=.087+.390*v
        fold=.0018*math.exp(-((v-.55)/.13)**2)*math.sin((v-.43)*math.pi*22)
        radius=.057*(1-v)+.032*v+.003*math.sin(math.pi*v)+fold
        angle=math.tau*u
        return (s*x,1.113+(radius+extra)*math.cos(angle),.036+(radius+extra)*math.sin(angle))
    skin_mode='sleeve'
    patch('relaxed set-in sleeve',sleeve,32,28,DENIM,0,lambda p,s=sign:(0,p[1]-1.113,p[2]-.036))
    skin_mode='cap'
    patch('closed sleeve shoulder',lambda u,v,s=sign:(s*(.087-.045*math.cos(v*math.pi/2)),
          1.113+.057*math.sin(v*math.pi/2)*math.cos(u*math.tau),
          .036+.057*math.sin(v*math.pi/2)*math.sin(u*math.tau)),32,8,DENIM,0,
          lambda p,s=sign:(-s,p[1]-1.113,p[2]-.036))
    skin_mode='sleeve'
    patch('structured cuff',lambda u,v,fn=sleeve:fn(u,.954+.046*v,extra=.0014),32,3,DARK,0,lambda p:(0,p[1]-1.113,p[2]-.036))
    for v in (.96,.993):stitch('cuff stitch',lambda u,v=v,fn=sleeve:fn(u,v,extra=.002),40)
    stitch('sleeve outer seam',lambda v,fn=sleeve:fn(.08,v,extra=.0011),80)
    button('cuff snap',sleeve(.75,.977,extra=.002),.0025)
# Back yoke: contrasting seam with two lines of warm thread.
skin_mode='torso'
for v in (.765,.788):stitch('back yoke seam',lambda u,v=v:torso(.26+.48*u,v,.002),68)
stitch('back centre seam',lambda v:torso(.5,v,.002),64)
for y in (1.014,1.046,1.080,1.112):button('front brass button',torso(.005,(y-.985)/.161,.004))
assert V and T and all(all(math.isfinite(x) for x in p) for p in V)
assert all(abs(sum(w[1::2])-1)<1e-5 and all(0<=j<nj for j in w[::2]) for w in W)
P=struct.Struct('<3f');S=struct.Struct('<2fII4f4I4f')
with (A/'denim-jacket.bin').open('wb') as f:
    f.write(struct.pack('<4sIII',b'DNMJ',1,len(V),len(T)*3))
    for p in V:f.write(P.pack(*p))
    for c in C:f.write(struct.pack('<4f',*c,1))
    for uv,n,flags,w in zip(UV,N,F,W):f.write(S.pack(*uv,0,flags,*n,0,*w[::2],*w[1::2]))
    for t in T:f.write(struct.pack('<3I',*t))
report={'name':'Midnight Indigo cropped denim jacket','version':1,'originalProjectDesign':True,
        'space':'Original Moca right-handed bind space; explicit chest/spine/arm-chain skin weights',
        'vertices':len(V),'triangles':len(T),'parts':dict(collections.Counter(p['name'] for p in parts)),
        'design':'Open front, cropped hem, folded collar, chest flap pockets, brass snaps, warm double topstitch',
        'skinning':'Original 315-joint hierarchy; GPU vertex skinning, no cloth simulation',
        'sha256':hashlib.sha256((A/'denim-jacket.bin').read_bytes()).hexdigest(),
        'characterSha256':hashlib.sha256(raw).hexdigest()}
(A/'denim-jacket.json').write_text(json.dumps(report,indent=2))
m=json.loads((A/'manifest.json').read_text(encoding='utf-8'))
for n in ('denim-jacket.bin','denim-jacket.json'):m['files'][n]=hashlib.sha256((A/n).read_bytes()).hexdigest()
(A/'manifest.json').write_text(json.dumps(m,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps(report,indent=2))
