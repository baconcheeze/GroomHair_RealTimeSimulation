"""Original Aster bust/groom authoring. Standard-library only; no downloaded art.

Writes the upstream HairTest binary scene schema and HAIR groom in WORLD METRES.
Jolt alone builds guides, frames, scalp attachments and render influences at load.
"""
import hashlib
import json
import math
import random
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "assets/aster"
OUT.mkdir(parents=True, exist_ok=True)
PI = math.pi
SKIN = (1.0, .78, .70)
IVORY = (.94, .90, .84)
INK = (.035, .025, .065)
PLUM = (.31, .16, .46)
GOLD = (.92, .66, .30)
TEAL = (.10, .23, .29)
verts, triangles, colors, joints = [], [], [], []


def sub(a, b):
    return tuple(x-y for x, y in zip(a, b))


def cross(a, b):
    return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])


def addv(p, color, joint=1, material=0):
    assert all(math.isfinite(x) for x in p)
    verts.append(p)
    colors.append((*color, material))
    joints.append(joint)
    return len(verts)-1


def tri(a, b, c):
    n = cross(sub(verts[b], verts[a]), sub(verts[c], verts[a]))
    if sum(x*x for x in n) > 1e-18:
        triangles.append((a, b, c))


CY, RY, RX, RZ = 1.466, .108, .093, .079


def jaw(ny):
    return 1.0 if ny >= -.20 else 1.0 - .28*((-ny-.20)/.80)**.80


def head(theta, phi):
    ny = math.cos(theta)
    return (RX*math.sin(theta)*math.sin(phi)*jaw(ny),
            CY+RY*ny, RZ*math.sin(theta)*math.cos(phi))


def facez(x, y):
    ny = (y-CY)/RY
    return RZ*math.sqrt(max(.015, 1-ny*ny-(x/(RX*jaw(ny)))**2))


def surface(fn, color, joint=1, lat=48, lon=96, material=0):
    base = len(verts)
    for i in range(lat+1):
        for j in range(lon+1):
            p = fn(PI*i/lat, 2*PI*j/lon)
            addv(p, color, joint, material)
    for i in range(lat):
        for j in range(lon):
            a = base+i*(lon+1)+j
            b = a+lon+1
            tri(a, b, b+1)
            tri(a, b+1, a+1)


def ellipsoid(center, radii, color, joint=1, lat=20, lon=40, material=0):
    surface(lambda t, p: (center[0]+radii[0]*math.sin(t)*math.sin(p),
                           center[1]+radii[1]*math.cos(t),
                           center[2]+radii[2]*math.sin(t)*math.cos(p)),
            color, joint, lat, lon, material)


def face_patch(cx, cy, rx, ry, color, lift=.0015, almond=False, material=0, bulge=0):
    """Conforming curved patch, front +Z; ordered CCW as seen from front."""
    rings, count = 8, 64
    start = len(verts)
    for ring in range(rings+1):
        r = ring/rings
        for j in range(count+1):
            a = 2*PI*j/count
            x = cx+rx*r*math.cos(a)
            y = cy+ry*r*math.sin(a)*(abs(math.sin(a))**.18 if almond else 1)
            if almond: y += .12*(1 if cx>0 else -1)*(x-cx)
            addv((x, y, facez(x, y)+lift+bulge*(1-r*r)), color, 1, material)
    for i in range(rings):
        for j in range(count):
            a = start+i*(count+1)+j
            b = a+count+1
            tri(a, b, b+1)
            tri(a, b+1, a+1)


# Sculpted anime silhouette: narrowed lower face, compact nose and expressive eyes.
surface(head, SKIN)
# Scalp root tint fills subpixel line gaps; no static hair shell is added.
for i,p in enumerate(verts):
    if p[1]>1.51: colors[i]=(.19,.07,.31,0)
for sign in [-1, 1]:
    ellipsoid((sign*.085, 1.433, -.001), (.012, .022, .011), SKIN)
    ellipsoid((sign*.091, 1.433, .006), (.0055, .014, .005), (.92, .57, .53))
    cx, cy = sign*.036, 1.456
    face_patch(cx, cy+.0008, .029, .0165, INK, .0018, True, 1)
    face_patch(cx, cy, .027, .0145, (.98, .97, 1), .0022, True, .65, .001)
    # Iris radial rings with a clear limbal outline and bright lower hemisphere.
    for radius, color in [(.0117, (.12, .055, .23)), (.0106, (.39, .20, .66)),
                          (.0086, (.58, .38, .83)), (.0047, (.055, .035, .13))]:
        face_patch(cx-sign*.001, cy, radius, radius*1.12, color, .0046+(.0117-radius)*.10, material=1)
    face_patch(cx-.0035, cy+.006, .0031, .0037, (1, 1, 1), .007, material=1)
    face_patch(cx+.003, cy-.006, .0015, .0018, (.94, .90, 1), .007, material=1)
    # Tapered eyeliner wing and eyebrow, explicit facial geometry.
    outer = cx+sign*.026
    ids = [addv((x, y, facez(x, y)+.0035), INK, 1, 1) for x, y in
           [(outer, cy+.004), (outer+sign*.009, cy+.012), (outer-sign*.005, cy+.011)]]
    tri(*ids) if sign > 0 else tri(ids[0], ids[2], ids[1])
    for k in range(20):
        u0, u1 = k/20, (k+1)/20
        points = []
        for u, d in [(u0, 0), (u1, 0), (u1, .0015), (u0, .0015)]:
            x = cx+(u-.5)*.043
            y = cy+.025+.004*math.sin(PI*u)+d
            points.append(addv((x, y, facez(x, y)+.0015), PLUM, 1, 1))
        tri(points[0], points[1], points[2]); tri(points[0], points[2], points[3])
    face_patch(sign*.057, 1.429, .013, .0038, (.99, .70, .70), .0005, material=.4)
    # Gold/lilac earrings follow the head joint, independently of simulated hair.
    ellipsoid((sign*.089, 1.403, .004), (.003, .008, .003), GOLD)
    ellipsoid((sign*.089, 1.389, .004), (.0045, .006, .004), (.58, .36, .78), material=.7)

# A small sculpted nose, no photoreal source mesh.
ellipsoid((0, 1.430, facez(0, 1.430)+.002), (.006, .012, .006), SKIN)
face_patch(0, 1.413, .010, .0020, (.79, .32, .42), .0015, material=.5)
face_patch(0, 1.414, .006, .0008, (1, .65, .68), .002, material=.6)

# Neck, tailored shoulders and a modest fitted upper-body presentation.
ellipsoid((0, 1.320, -.005), (.032, .065, .031), SKIN, 2, lat=28, lon=48)
# Tailored bust, open collar to flat elliptical base; not a spherical torso.
profile=[(.995,.115,.063),(1.06,.120,.068),(1.16,.140,.077),(1.235,.151,.063),(1.275,.110,.046),(1.302,.037,.031)]
start=len(verts);rings=40;lon=96
for i in range(rings+1):
    u=i/rings*(len(profile)-1);k=min(len(profile)-2,int(u));a=u-k
    y,rx,rz=[profile[k][j]*(1-a)+profile[k+1][j]*a for j in range(3)]
    for j in range(lon+1):
        ph=2*PI*j/lon;addv((rx*math.sin(ph),y,-.010+rz*math.cos(ph)),TEAL,0)
for i in range(rings):
    for j in range(lon):
        a=start+i*(lon+1)+j;b=a+lon+1;tri(a,b+1,b);tri(a,a+1,b+1)
for sign in [-1, 1]:
    ellipsoid((sign*.133, 1.243, -.012), (.055, .040, .055), TEAL, 0)
    # Ivory collar petals and gold seam accents.
    ellipsoid((sign*.032, 1.288, .042), (.034, .018, .008), IVORY, 0)
    ellipsoid((sign*.055, 1.279, .048), (.003, .021, .003), GOLD, 0)
    ellipsoid((sign*.020, 1.268, .059), (.024, .012, .006), (.54, .37, .70), 0)
ellipsoid((0, 1.268, .066), (.008, .012, .006), GOLD, 0)
ellipsoid((0, 1.269, .072), (.0048, .007, .003), (.55, .30, .78), 0, material=.8)
ellipsoid((0, .994, -.010), (.110, .010, .061), (.07, .12, .18), 0)


def quat_mul(a, b):
    x,y,z,w = a; X,Y,Z,W = b
    return (w*X+x*W+y*Z-z*Y, w*Y+y*W+z*X-x*Z,
            w*Z+z*W+x*Y-y*X, w*W-x*X-y*Y-z*Z)


def axis_q(axis, angle):
    s = math.sin(angle*.5)
    return (*[v*s for v in axis], math.cos(angle*.5))


def hull_points(center, radii, shaped=False):
    return [head(PI*i/12, 2*PI*j/24) if shaped else
            (center[0]+radii[0]*math.sin(PI*i/12)*math.sin(2*PI*j/24),
             center[1]+radii[1]*math.cos(PI*i/12),
             center[2]+radii[2]*math.sin(PI*i/12)*math.cos(2*PI*j/24))
            for i in range(13) for j in range(24)]


# Same source points as the visible head, slightly inset. Other convex volumes
# are authored against the neck/coat/shoulders, never inherited from the sample.
head_hull = [(x*.975, CY+(y-CY)*.975, z*.975) for x,y,z in hull_points(None,None,True)]
hulls = [(1, head_hull),
         (1, hull_points((0, 1.323, -.005), (.029, .056, .028))),
         (0, hull_points((0, 1.150, -.010), (.140, .144, .066))),
         (0, hull_points((-.130, 1.243, -.012), (.051, .036, .050))),
         (0, hull_points((.130, 1.243, -.012), (.051, .036, .050)))]


def pack(fmt, *v):
    return struct.pack('<'+fmt, *v)


with (OUT/'character.bin').open('wb') as f:
    f.write(pack('II', 1, len(verts)))
    for p in verts: f.write(pack('3f', *p))
    f.write(pack('I', len(triangles)))
    for t in triangles: f.write(pack('3I', *t))
    f.write(pack('I', 2))
    for y in [1.15, 1.36]:
        f.write(pack('16f', 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,-y,0,1))
    f.write(pack('I', 2))
    for p, joint in zip(verts, joints):
        w = max(0.,min(1.,(p[1]-1.255)/.095)) if joint==2 else float(joint)
        f.write(pack('IfIf', 0, 1-w, 1, w))
    frames = 240
    f.write(pack('I', frames))
    for i in range(frames):
        phase = 2*PI*i/frames
        for joint in range(2):
            if joint:
                yaw = .28*math.sin(phase)
                pitch = .105*math.sin(2*phase)
                roll = .065*math.sin(phase)
                q = quat_mul(quat_mul(axis_q((0,1,0),yaw),axis_q((1,0,0),pitch)),axis_q((0,0,1),roll))
                pos = (.005*math.sin(phase),1.36+.002*math.sin(2*phase),0)
            else:
                q = axis_q((0,1,0),.025*math.sin(phase))
                pos = (0,1.15,0)
            f.write(pack('6f', *pos, *q[:3]))
    f.write(pack('I', len(hulls)))
    for joint, points in hulls:
        f.write(pack('II', joint, len(points)))
        for p in points: f.write(pack('3f', *p))

with (OUT/'colors.bin').open('wb') as f:
    f.write(pack('I', len(colors)))
    for c in colors: f.write(pack('4f', *c))

# Smooth display normals, with the same two-joint skin weight as character.bin.
normals=[[0.,0.,0.] for p in verts]
for a,b,c in triangles:
    n=cross(sub(verts[b],verts[a]),sub(verts[c],verts[a]))
    for i in (a,b,c):
        for j in range(3):normals[i][j]+=n[j]
with (OUT/'normals.bin').open('wb') as f:
    f.write(pack('I',len(verts)))
    for p,n,joint in zip(verts,normals,joints):
        length=math.sqrt(sum(v*v for v in n))
        n=[v/length for v in n] if length>1e-20 else [0.,1.,0.]
        weight=max(0.,min(1.,(p[1]-1.255)/.095)) if joint==2 else float(joint)
        f.write(pack('4f',*n,weight))

# Original groom: layered long back/sides + five curved fringe regions.
# Actual authored strands, not a static triangle hair shell. All are passed
# to Jolt's own guide selection / influence construction without a custom solver.
rng = random.Random(194672)
strand_count, points_per_strand = 16000, 32
points = []
for i in range(strand_count):
    fringe = i % 5 == 0
    if fringe:
        theta0 = .045+.855*rng.random()
        section = (i//5)%5
        phi0 = -.78+(section+rng.random())*(1.56/5)
        phi_end = phi0+.14*math.sin(phi0*3)
        theta_end = 1.20+.20*abs(math.sin(phi0*3.5))+.04*rng.random()
        for k in range(points_per_strand):
            t=k/(points_per_strand-1)
            th=theta0+(theta_end-theta0)*t
            ph=phi0+(phi_end-phi0)*t*t
            x,y,z=head(th,ph)
            lift=.006*math.sin(PI*t*.8)
            points.append((x+lift*math.sin(ph),y+.0005*math.sin(PI*t),z+lift*math.cos(ph)))
    else:
        theta0=.045+1.215*rng.random()
        phi=.92+(2*PI-1.84)*rng.random()
        cap=max(.001,(PI/2+.06-theta0)*.105)
        drop=.235+.045*rng.random()+.018*math.cos(phi)
        for k in range(points_per_strand):
            t=k/(points_per_strand-1)
            length=t*(cap+drop)
            if length<=cap:
                th=theta0+length/.105
                x,y,z=head(th,phi)
                lift=.003*math.sin(PI*.5*length/cap)
                x+=lift*math.sin(phi);z+=lift*math.cos(phi)
            else:
                u=(length-cap)/drop
                x,y,z=head(PI/2+.06,phi)
                wave=.005*math.sin(PI*u*1.5+phi*.5)*u+.005*u*u
                x+=(.003+wave)*math.sin(phi)
                z+=(.003+wave)*math.cos(phi)
                y-=length-cap
            points.append((x,y,z))

header=bytearray(128)
struct.pack_into('<4sIIII',header,0,b'HAIR',strand_count,len(points),2,points_per_strand-1)
with (OUT/'amethyst.hair').open('wb') as f:
    f.write(header)
    for p in points: f.write(pack('3f',*p))

# Material separation is consumed by upstream Jolt guide/influence selection.
with (OUT/'groom.materials').open('wb') as f:
    f.write(pack('I', strand_count))
    for i in range(strand_count): f.write(pack('I', 1 if i%5==0 else 0))

manifest=dict(formatVersion=1,faceSchema='upstream HairTest v5.6.0',name='Aster — Amethyst Atelier',original=True,units='metres',
              space='Jolt right-handed +Y up; front +Z; world bind coordinates',
              headJoint=1,joints=2,animationFrames=240,animationHz=60,
              vertices=len(verts),triangles=len(triangles),collisionHulls=len(hulls),
              authoredStrands=strand_count,verticesPerStrand=32,fringeStrands=3200,
              seed=194672,license='Project-owned original content; see LICENSE.txt',
              files={n:hashlib.sha256((OUT/n).read_bytes()).hexdigest()
                     for n in ['character.bin','colors.bin','normals.bin','amethyst.hair','groom.materials']})
(OUT/'manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
(OUT/'LICENSE.txt').write_text('Original Aster character, groom and animation authored for HairLabDX12.\nThese original assets are project-controlled; licensing is determined by the project owner. No public-domain dedication is implied.\nNo third-party character model, groom, texture or franchise identity is used. Jolt and ImGui retain their separate licenses.\n',encoding='utf-8')
print(json.dumps(manifest,ensure_ascii=True,indent=2))
