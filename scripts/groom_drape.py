"""One coherent scalp drape, not a stack of axis-aligned point repairs.
Offline content authoring only. Simulation and reconstruction remain Jolt.
"""
import math,bmesh
import numpy as np
from mathutils import Vector
from mathutils.kdtree import KDTree
from mathutils.bvhtree import BVHTree

def smooth(a,b,x):
    t=max(0.,min(1.,(x-a)/(b-a)));return t*t*(3-2*t)

def resample(points,count=32):
    p=np.array(points,dtype=float);length=np.linalg.norm(np.diff(p,axis=0),axis=1)
    arc=np.r_[0.,np.cumsum(length)];assert arc[-1]>1e-6
    return [Vector(v) for v in np.stack([np.interp(np.linspace(0,arc[-1],count),arc,p[:,k]) for k in range(3)],axis=1)]

class ScalpDrape:
    def __init__(self,surface,bvh,ears,wisp_path):
        self.surface,self.bvh,self.ears=surface,bvh,ears;self.center=Vector(surface.center)
        self.root_repairs=0;self.segment_repairs=0;self.max_root=0.;self.curves=0
        import struct
        blob=wisp_path.read_bytes();magic,version,count,n=struct.unpack_from('<4sIII',blob)
        assert magic==b'WISP' and version==1 and count==117 and n==32 and len(blob)==16+count*(4+32*12)
        self.wisps={};offset=16
        for _ in range(count):
            index=struct.unpack_from('<I',blob,offset)[0];offset+=4
            self.wisps[index]=[Vector(p) for p in struct.iter_unpack('<3f',blob[offset:offset+384])];offset+=384
        def triangles(n):return n[2] if n[3] is None else triangles(n[2])+triangles(n[3])
        tris=triangles(surface.tree)
        def auricle(p):return abs(p[0])>.056 and 1.202<p[1]<1.279 and .004<p[2]<.060
        self.nape_bvh=BVHTree.FromPolygons(surface.v,[t for t in surface.head_triangles_all if min(surface.v[i][2] for i in t)>.032 and not any(auricle(surface.v[i]) for i in t)],all_triangles=True)
        # The head collider previously included the auricles, bridging the
        # concavity behind them and forcing nearby hair to hover.
        self.skull_points=[Vector(p) for i,p in enumerate(surface.v) if (i<13320 or i>=len(surface.v)-353) and p[1]>1.19 and not auricle(p)]
        skull_tris=[t for t in tris if not any(auricle(surface.v[i]) for i in t)]
        self.skull_bvh=BVHTree.FromPolygons(surface.v,skull_tris,all_triangles=True)
        self.nt,self.np=81,128;self.max_theta=2.60
        radii=np.full((self.nt,self.np),np.nan)
        for i in range(self.nt):
            theta=i*self.max_theta/(self.nt-1)
            for j in range(self.np):
                phi=j*2*math.pi/self.np;d=self.direction(theta,phi)
                hit=self.skull_bvh.ray_cast(self.center,d,.25)[0]
                if hit is not None:radii[i,j]=(hit-self.center).length
        # Fill only holes in the measured surface, then smooth a few mm across
        # neighboring directions. No substitute head primitive is generated.
        for _ in range(100):
            missing=np.isnan(radii)
            if not missing.any():break
            total=np.zeros_like(radii);weight=np.zeros_like(radii)
            for axis in (0,1):
                for shift in (-1,1):
                    n=np.roll(radii,shift,axis);valid=np.isfinite(n);total+=np.where(valid,n,0);weight+=valid
            usable=missing&(weight>0);radii[usable]=total[usable]/weight[usable]
        assert np.isfinite(radii).all()
        for _ in range(3):
            radii=(np.roll(radii,1,1)+radii*2+np.roll(radii,-1,1))*.25
            padded=np.pad(radii,((1,1),(0,0)),mode='edge');radii=(padded[:-2]+padded[1:-1]*2+padded[2:])*.25
        radii[0,:]=radii[0].mean();self.radii=radii
        self.tie={-1:Vector((.001, .001,-.004)),1:Vector((-.001,.001,-.004))}
        self.tie_trees={}
    @staticmethod
    def direction(theta,phi):return Vector((math.sin(theta)*math.sin(phi),math.cos(theta),-math.sin(theta)*math.cos(phi)))
    def polar(self,p):
        d=p-self.center;r=d.length
        return r,math.acos(max(-1,min(1,d.y/r))),math.atan2(d.x,-d.z)
    def radius(self,theta,phi):
        u=max(0.,min(self.nt-1.000001,theta*(self.nt-1)/self.max_theta));v=(phi%(2*math.pi))*self.np/(2*math.pi)
        i,j=int(u),int(v);a,b=u-i,v-j;g=self.radii
        return float((g[i,j]*(1-b)+g[i,(j+1)%self.np]*b)*(1-a)+(g[i+1,j]*(1-b)+g[i+1,(j+1)%self.np]*b)*a)
    def root(self,p,region,record=True):
        if region in (1,3,4):
            # Front locks already have an authored hairline. Moving those
            # follicles with the swept-cap boundary lifts the curl into the ear.
            hit=self.bvh.find_nearest(p)[0]
            if all(not ear.near(hit) or ear.separation(hit)[0]>.0018 for ear in self.ears):
                if record:self.root_repairs+=(hit-p).length>1e-5;self.max_root=max(self.max_root,(hit-p).length)
                return hit
        _,theta,phi=self.polar(p)
        if region==7:
            # Nape hairs belong behind the ear. Mapping them with the side-cap
            # hairline raised roots over the ear, and a rear ray could then hit
            # the front of the face through the missing lateral back surface.
            a=(abs(phi)-2.45)/.08
            phi=math.copysign(2.45+.08*(max(a,0)+math.log1p(math.exp(-abs(a)))),phi)
        back=(1-math.cos(phi))*.5
        limit=1.15+1.32*back**1.25+(.07 if region in (3,4) else 0.)
        if region==0:
            limit=1.15+1.30*smooth(1.1,2.0,abs(phi))
        ear_weight=0. if region in (2,7) else smooth(.7,1.1,abs(phi))*(1-smooth(2.1,2.5,abs(phi)))
        if region==0:
            ear_weight=smooth(.7,1.1,abs(phi))*(1-smooth(1.8,2.15,abs(phi)))
        # The old 1.43-radian cap lifted a broad patch ABOVE the auricle,
        # producing a bald triangle. Dress to the ear rim, then resolve only
        # actual ear contacts below instead of excluding the whole side.
        ear_limit=1.85 if region==0 else 1.43
        limit=limit*(1-ear_weight)+min(limit,ear_limit)*ear_weight
        width=.055;z=(theta-limit)/width
        theta-=width*(max(z,0)+math.log1p(math.exp(-abs(z))))
        target=self.center+self.direction(theta,phi)*self.radius(theta,phi)
        hit=self.skull_bvh.find_nearest(target)[0]
        if hit is None:raise ValueError('Missing scalp attachment')
        for _ in range(16):
            if all(not ear.near(hit) or ear.separation(hit)[0]>.0018 for ear in self.ears):break
            theta-=.025;target=self.center+self.direction(theta,phi)*self.radius(theta,phi)
            hit=self.skull_bvh.find_nearest(target)[0]
        else:raise ValueError('Unable to place follicle clear of auricle')
        if record:self.root_repairs+=(hit-p).length>1e-5;self.max_root=max(self.max_root,(hit-p).length)
        return hit
    def set_ties(self,corrections):
        for side,values in corrections.items():
            if values:self.tie[side]=sum(values,Vector())/len(values)
    def set_tie_anchors(self,anchors):
        for side,values in anchors.items():
            tree=KDTree(len(values))
            for i,p in enumerate(values):tree.insert(p,i)
            tree.balance();self.tie_trees[side]=tree
    def shell(self,p,arc,t):
        _,theta,phi=self.polar(p)
        crown=1-smooth(.45,1.45,theta)
        lift=(.0023+.006*crown)*(1-math.exp(-arc/.010))*(1-.55*smooth(.82,1.,t))
        return self.center+self.direction(theta,phi)*(self.radius(theta,phi)+lift)
    def protect(self,p,margin=.0015,face_corridor=False):
        d=p-self.center
        if p.y>1.185 and d.length<.19:
            # Never use an ear as a radial skull boundary. Its shadow falsely
            # pushes points behind the ear outward and creates folded CVs.
            direction=d.normalized();hit=(self.bvh if face_corridor else self.skull_bvh).ray_cast(self.center,direction,.24)[0]
            if hit is not None and (hit-self.center).length+margin>d.length:
                p=self.center+direction*((hit-self.center).length+margin)
        for ear in (() if face_corridor else self.ears):
            lo,hi=ear.bounds
            if all(lo[k]-.0023<p[k]<hi[k]+.0023 for k in range(3)):p=ear.resolve(p,.0022)
        return p
    def nape_contact(self,p):
        # A posterior ray preserves the curl's tangential x/y coordinates;
        # nearest-triangle snapping can fold multiple samples onto one corner.
        hit,n,_,_=self.nape_bvh.find_nearest(p)
        if n.dot(hit-self.center)<0:n=-n
        return hit+n*.0018
    def apply(self,original,region,index,group=''):
        self.curves+=1
        if region==4:
            # Keep the fitted source curls, rather than straightening their
            # signature shape while fitting the unrelated swept-cap surface.
            p=[v.copy() for v in self.wisps[index]];delta=p[0]-original[0]
            self.root_repairs+=delta.length>1e-5;self.max_root=max(self.max_root,delta.length)
            return p
        if region==0:
            # The previous posterior hairline stopped at theta=2 (~1.24m),
            # exposing a broad bald strip above the nape. Extend the actual
            # swept strands over the occiput; preserve the front and ear edge.
            dressed=[]
            for v in original:
                radius,theta,phi=self.polar(v)
                weight=smooth(1.50,2.15,abs(phi))*smooth(1.00,1.95,theta)
                side=smooth(.95,1.45,abs(phi))*(1-smooth(1.85,2.15,abs(phi)))
                theta+=.46*weight+.28*side*smooth(1.00,1.85,theta)
                dressed.append(self.center+self.direction(theta,phi)*radius)
            original=dressed
        root=self.root(original[0],region);correction=root-original[0]
        lengths=[0.]
        for a,b in zip(original,original[1:]):lengths.append(lengths[-1]+(b-a).length)
        total=lengths[-1];fractions=[x/total for x in lengths]
        if region==0:
            side=-1 if original[-1].x<0 else 1;tie=self.tie[side]
            anchor=self.tie_trees[side].find(original[-1]+tie)[0]
            _,normal,_,_=self.bvh.find_nearest(anchor)
            if normal.dot(anchor-self.center)<0:normal=-normal
            anchor=self.protect(anchor+normal*.0018,.0015)
            p=[v+correction*(1-smooth(0,1,t))+tie*smooth(0,1,t) for v,t in zip(original,fractions)]
            p[0]=root
            p=[root]+[self.shell(v,a,t) for v,a,t in zip(p[1:],lengths[1:],fractions[1:])]
            offset=anchor-p[-1]
            p=[v+offset*smooth(.72,1.,t) for v,t in zip(p,fractions)];p[-1]=anchor
            # Geodesic fairing over the complete cap: crown, side, underneath
            # and nape share one smooth field; no hard top/side ROI seams.
            for _ in range(14):
                q=[root]+[p[j]*.60+(p[j-1]+p[j+1])*.20 for j in range(1,31)]+[p[-1]]
                p=[root]+[self.shell(v,a,t) for v,a,t in zip(q[1:],lengths[1:],fractions[1:])]
                offset=anchor-p[-1]
                p=[v+offset*smooth(.72,1.,t) for v,t in zip(p,fractions)];p[-1]=anchor
            p=resample(p);p[0]=root
        elif region==2:
            # The root cluster is one gathered bundle, not 5,177 unrelated
            # translations. Keep its full body coherent; only the short scalp
            # attachment fades to the individually fixed follicle.
            side=-1 if original[0].x<0 else 1;shared=self.tie[side]
            delta=root-(original[0]+shared)
            p=[v+shared+delta*(1-smooth(0,.015,a)) for v,a in zip(original,lengths)]
        else:
            p=[v+correction for v in original]
            if region==7:
                delta=[Vector()]
                for j in range(1,32):
                    target=self.nape_contact(p[j])
                    delta.append((target-p[j])*smooth(0,.010,lengths[j]))
                for _ in range(12):
                    delta=[Vector()]+[delta[j]*.6+(delta[j-1]+delta[min(31,j+1)])*.2 for j in range(1,32)]
                p=[v+d for v,d in zip(p,delta)]
        reference=[v.copy() for v in p]
        # Fair the *correction*, preserving bangs/curls/waves. Spreading contact
        # displacements along the curve prevents a new single-CV horn.
        for _ in range(14):
            deltas=[p[j]-reference[j] for j in range(32)]
            q=[root]
            for j in range(1,32):
                d=deltas[j]*.6+(deltas[j-1]+deltas[min(31,j+1)])*.2
                margin=.002*smooth(0,.010,lengths[j]) if region==0 else .0013
                q.append(self.protect(reference[j]+d,margin,face_corridor=region==4))
            p=q
        # Resolve any remaining continuous ear chord, then spread its repair.
        for attempt in range(16):
            changed=False;before=[v.copy() for v in p]
            for j in range(31):
                for ear in self.ears:
                    a,b,repair=ear.detour_segment(p[j],p[j+1],j==0,.0015)
                    if repair:p[j],p[j+1]=a,b;changed=True;self.segment_repairs+=1
            if not changed:break
            delta=[p[j]-before[j] for j in range(32)]
            for j in range(1,32):
                spread=(delta[j-1]+delta[min(31,j+1)])*.3
                p[j]=self.protect(p[j]+spread,.0015)
        # Nearest-surface contact can map several nape samples to one mesh
        # corner. Arc-length sampling removes zero-length rods without
        # deleting a strand or substituting a procedural curl.
        if region==7:p=resample(p)
        return p
    def write_skull(self,path):
        import struct
        bm=bmesh.new();vs=[bm.verts.new(p) for p in self.skull_points]
        bmesh.ops.convex_hull(bm,input=vs,use_existing_faces=False)
        used=sorted(set(v for f in bm.faces for v in f.verts),key=lambda v:tuple(v.co))
        with path.open('wb') as f:
            f.write(struct.pack('<4sII',b'SKUL',1,len(used)))
            for v in used:f.write(struct.pack('<3f',*v.co))
        self.skull_count=len(used);bm.free()
    def record(self):
        return {'method':'Smooth measured skull drape, continuous anatomical hairline, shared tie endpoints, geodesic cap fairing, contact-displacement fairing',
                'rootRepairs':int(self.root_repairs),'maxRootRepairMetres':self.max_root,'curves':self.curves,'earSegmentRepairs':self.segment_repairs,
                'crownLiftMetres':.0083,'sideLiftMetres':.0023,'skullColliderPoints':self.skull_count}
