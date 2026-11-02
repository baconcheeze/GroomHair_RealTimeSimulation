"""Author a continuous side-part salon bob; retain long groom bits.
Fits roots and crown to the repaired/inset scalp, without editing the long groom.
"""
import hashlib,json,math,random,struct
from groom_surface import ScalpSurface
from pathlib import Path
R=Path(__file__).resolve().parents[1];A=R/'assets/moca'
src=(A/'studio.hair').read_bytes();magic,sourceCount,npoints,flags,nseg=struct.unpack_from('<4sIIII',src)
assert magic==b'HAIR' and sourceCount==16000 and nseg==31
count=32000
material_ids=[]
N=nseg+1;rng=random.Random(320615);points=[];cx,cy,cz=0,1.270,.022
surface=ScalpSurface(A/'character.bin')
def smooth(t):return t*t*(3-2*t)
def crown(theta,phi):
    # Smooth authoring envelope, clear of eyelids/ear detail. Real roots remain
    # on the repaired mesh; sampling facial detail was the old temple kink.
    return (.086*math.sin(theta)*math.sin(phi),cy+.106*math.cos(theta),cz-.086*math.sin(theta)*math.cos(phi))
def bezier(c,t):
    w=[(1-t)**3,3*(1-t)**2*t,3*(1-t)*t*t,t**3]
    return tuple(sum(w[j]*c[j][k] for j in range(4)) for k in range(3))
def resample(poly,n):
    lengths=[0.]
    for a,b in zip(poly,poly[1:]):lengths.append(lengths[-1]+math.dist(a,b))
    assert lengths[-1]>1e-5
    result=[];j=0
    for i in range(n):
        s=lengths[-1]*i/(n-1)
        while j+1<len(lengths)-1 and lengths[j+1]<s:j+=1
        u=(s-lengths[j])/max(1e-15,lengths[j+1]-lengths[j])
        result.append(tuple(poly[j][k]+u*(poly[j+1][k]-poly[j][k]) for k in range(3)))
    return result
for i in range(count):
    front=i%5==0
    theta=.045+(.79 if front else 1.15)*rng.random()
    # The old minimum polar angle left a bare disk at the crown. Redistribute
    # only the innermost roots across that disk, with exact scalp attachment.
    if theta<.12:theta=.12*(theta-.045)/(.12-.045)
    phi=(-1.15+2.30*rng.random()) if front else (.82+(2*math.pi-1.64)*rng.random())
    root=surface.ray(theta,phi);x,y,z=root
    side=1 if phi>.22 else -1
    layer=rng.random();jitter=(rng.random()-.5)
    # Thin only the forehead curtain. Still consume the same random samples
    # so every existing side/back strand retains its authored shape.
    if front and (i//5)%6!=0:
        continue
    material_ids.append(int(front))
    if front:
        # Reference2: a light brow-length fringe, with feathered ends and
        # longer temple corners. Preserve root sampling and all non-front bits.
        edge=abs(phi)/1.15
        endphi=phi*.74
        endtheta=1.12
        initial=crown(theta,phi);offset=tuple(root[k]-initial[k] for k in range(3));poly=[]
        for j in range(65):
            u=j/64;th=theta+(endtheta-theta)*u
            ph=phi+(endphi-phi)*smooth(u)
            p=crown(th,ph);w=1-smooth(u)
            lift=(.014+.004*layer)*smooth(min(1,(endtheta-theta)*.09*u/.010))
            normal=(.55*math.sin(th)*math.sin(ph),.95*math.cos(th),-.20*math.sin(th)*math.cos(ph))
            poly.append(tuple(p[k]+offset[k]*w+lift*normal[k] for k in range(3)))
        # Fit the lower fringe to the actual forehead instead of extending
        # the crown's generous volume envelope in front of the face.
        forehead=surface.ray(endtheta,endphi)
        fittedJoin=(forehead[0],forehead[1]+.002,forehead[2]-.003)
        correction=tuple(fittedJoin[k]-poly[-1][k] for k in range(3))
        poly=[tuple(p[k]+correction[k]*(j/64)**2 for k in range(3)) for j,p in enumerate(poly)]
        join=poly[-1]
        corner=edge**8
        rawx=join[0]*1.01
        # Continuous soft sweep, never quantized into broad comb-like locks.
        # A small off-centre opening fades smoothly, with independent fine
        # ends rather than six repeated rectangular sections.
        partSide=-1 if rawx<.012 else 1
        tipx=rawx-.006*(1-edge*edge)+.004*partSide+.0015*jitter
        # The stronger regional hold must start from the desired brow-length
        # shape, not a short airborne shape that relies on gravity settling.
        partFeather=.005*math.exp(-((rawx-.012)/.014)**2)
        tipy=1.268-.008*corner+.014*layer**2.5+.004*jitter+.0025*math.sin(phi*2+.4)+partFeather
        tipz=surface.front(tipx,tipy)[2]-.003-.0005*layer
        tip=(tipx,tipy,tipz)
        tangent=tuple(join[k]-poly[-2][k] for k in range(3))
        tangentLength=math.sqrt(sum(v*v for v in tangent))
        c=[join,tuple(join[k]+tangent[k]/tangentLength*.014 for k in range(3)),
           (tipx,tipy+.014,tipz-.002),tip]
        poly.extend(bezier(c,j/64) for j in range(1,65))
        strand=resample(poly,N);strand[0]=root;points.extend(strand)
        continue
    if front:
        # Sweep the entire front into long cheek framing: no isolated wire-like
        # forehead hairs or separate short cap with a different cut line.
        spread=max(0,min(1,abs(phi-.22)/1.3+.025*jitter))
        endphi=side*(.86+.48*spread+.018*jitter)
        endtheta=1.65+.04*spread
    else:
        endphi=phi+.025*math.sin(2*phi)
        endtheta=1.45+.025*math.cos(phi)
    initial=crown(theta,phi);offset=tuple(root[k]-initial[k] for k in range(3));poly=[]
    for j in range(65):
        u=j/64;th=theta+(endtheta-theta)*u
        # Let the larger curtain sweep lower across the temple before joining
        # the perimeter, rather than combing every front root straight back.
        sweep=smooth(u)**(1.3 if front and side<0 else 1.0)
        ph=phi+(endphi-phi)*sweep
        p=crown(th,ph);w=1-smooth(u)
        # Layers broaden gently without changing the root or creating a step.
        loft=.0015*layer*math.sin(math.pi*u)
        # Lift rises smoothly over the first 10 mm of crown travel, leaving
        # the root exactly attached. Most loft is upward/sideways, not backward.
        travel=(endtheta-theta)*.09*u
        rootLift=(.014+.004*layer)*smooth(min(1,travel/.010))
        normal=(.55*math.sin(th)*math.sin(ph),.95*math.cos(th),-.20*math.sin(th)*math.cos(ph))
        poly.append(tuple(p[k]+offset[k]*w+loft*v+rootLift*normal[k] for k,v in enumerate((math.sin(ph),.3,-math.cos(ph)))))
    join=poly[-1];radial=(math.sin(endphi),0,-math.cos(endphi))
    # Preserve the outer bob perimeter but tuck shorter inner graduation below
    # it. Reuse the same root/depth samples: no separate physics or sparse cap.
    innerCut=.009*(1-layer)**1.6
    faceCut=.012*(1-smooth(spread)) if front else 0.
    # Shallow point-cut texture rather than a horizontal, ruler-straight hem.
    pointCut=.003*(.5+.5*math.sin(17*endphi+2.5*layer))**2+.0015*(jitter+.5)
    tipy=1.170+.005*(.5-.5*math.cos(endphi))+innerCut+faceCut+pointCut
    drop=join[1]-tipy
    tangent=tuple(poly[-1][k]-poly[-2][k] for k in range(3))
    length=math.sqrt(sum(v*v for v in tangent));tangent=tuple(v/length for v in tangent)
    tuck=.004+.003*(1-layer)
    tip=(join[0]-tuck*radial[0],tipy,join[2]-tuck*radial[2])
    c=[join,tuple(join[k]+tangent[k]*drop*.35 for k in range(3)),
       (tip[0]+.003*radial[0],tip[1]+.026,tip[2]+.003*radial[2]),tip]
    poly.extend(bezier(c,j/64) for j in range(1,65))
    strand=resample(poly,N);strand[0]=root;points.extend(strand)
count=len(material_ids)
with (A/'bob.hair').open('wb') as f:
    header=bytearray(src[:128]);struct.pack_into('<II',header,4,count,len(points));f.write(header)
    for p in points:f.write(struct.pack('<3f',*p))
assert hashlib.sha256((A/'studio.hair').read_bytes()).digest()==hashlib.sha256(src).digest()
with (A/'bob.materials').open('wb') as f:
    f.write(struct.pack('<I',count))
    for material in material_ids:f.write(struct.pack('<I',material))
report={'name':'Soft swept wispy fringe bob','revision':'salon-v10-soft-fringe','reference':'assets/reference2.avif','referenceSha256':hashlib.sha256((R/'assets/reference2.avif').read_bytes()).hexdigest(),
        'strands':count,'pointsPerStrand':N,'seed':320615,'roots':'Exact triangle raycasts onto current repaired/inset scalp; native Jolt attachment',
        'scalpSha256':hashlib.sha256((A/'character.bin').read_bytes()).hexdigest(),'authoring':'Airy softly swept fringe with fine independently graded brow-length ends and one off-centre part. Remove six hard clustered sections; thin forehead strands further to one sixth of original. Forehead fit, retained roots, all 25600 side/back strands and regional physics retained.',
        'cutMetres':{'innerGraduationMax':.009,'faceFramingMax':.012,'pointCutMax':.0045,'tipTuck':[.004,.007]},
        'volumeMetres':{'crownRadii':[.086,.106,.086],'rootLift':[.014,.018],'rootLiftRampLength':.010,'rootAttachmentPreserved':True,'crownRootCoverage':'Redistributed theta<0.12 to include the old bare polar disk'},
        'fringe':{'strandCount':sum(material_ids),'previousStrandCount':6400,'tipBaseYMetres':1.268,'upperLayerShorteningMaxMetres':.014,'partFeatherMaxMetres':.005,'templeLengthExtraMaxMetres':.008,'foreheadTipClearanceMetres':[.003,.0035],'reference':'assets/reference2.avif','settledCheckSteps':120},
        'materialsFile':'bob.materials','guidePercentage':5.0,
        'longGroomSha256':hashlib.sha256(src).hexdigest(),'bobSha256':hashlib.sha256((A/'bob.hair').read_bytes()).hexdigest()}
(A/'bob.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
manifest=json.loads((A/'manifest.json').read_text(encoding='utf-8'))
for name in ('bob.hair','bob.json','bob.materials'):manifest['files'][name]=hashlib.sha256((A/name).read_bytes()).hexdigest()
(A/'manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps(report,ensure_ascii=True,indent=2))
