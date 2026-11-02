"""Local scalp seating and continuous ear clearance, offline authoring only."""
from mathutils import Vector

def smoothstep(a,b,x):
    t=max(0.,min(1.,(x-a)/(b-a)))
    return t*t*(3.-2.*t)

class SideHairFinish:
    def __init__(self,surface,bvh,ears):
        self.surface,self.bvh,self.ears=surface,bvh,ears
        self.root_repairs=0;self.seated_points=0;self.segment_repairs=0
        self.max_seat=0.;self.max_root=0.
        self.head_repairs=0
    def head_clearance(self,p,clearance):
        if not (.032<abs(p.x)<.13 and 1.185<p.y<1.335 and -.055<p.z<.125):return Vector()
        origin=Vector(self.surface.center);radial=p-origin
        if radial.length>.19:return Vector()
        direction=radial.normalized();hit=self.bvh.ray_cast(origin,direction,.25)[0]
        if hit is None:return Vector()
        depth=(hit-origin).length+clearance-radial.length
        return direction*max(0.,depth)
    def root(self,p,region):
        origin=Vector(self.surface.center)
        if region==0 and p.y<1.238 and p.z>.030:
            # Nape follicles were landing on the neck, inside its moving
            # collider. Keep them on the occipital scalp, not on neck skin.
            target=p.copy();target.y=1.245;target.z=max(target.z,.064)
            hit=self.bvh.ray_cast(origin,(target-origin).normalized(),.3)[0]
            if hit is None:raise ValueError('Missing occipital scalp')
            self.root_repairs+=1;self.max_root=max(self.max_root,(hit-p).length)
            return hit
        for ear in self.ears:
            lo,hi=ear.bounds
            medial=(abs(p.x)>.035 and lo[1]-.006<p.y<hi[1]+.006 and lo[2]-.004<p.z<hi[2]+.004 and p.x*ear.side>0)
            if not medial and (not ear.near(p) or ear.separation(p)[0]>.004):continue
            # A follicle belongs to the skull above the auricle, not to the
            # ear itself. The old nearest-triangle projection attached 543
            # follicles to the ear and their first edges crossed its volume.
            target=p.copy()
            if p.y<ear.center.y+.008:
                target.z=max(target.z,hi[2]+.018);target.y=max(target.y,1.245)
            else:target.y=max(target.y,hi[1]+.009)
            hit=self.bvh.ray_cast(origin,(target-origin).normalized(),.3)[0]
            if hit is None:raise ValueError('Missing supra-auricular scalp')
            self.root_repairs+=1;self.max_root=max(self.max_root,(hit-p).length)
            return hit
        return p
    def apply(self,strand,region,strand_index):
        if region==0:
            # Seat only the temple/posterior cap, with a smooth transition to
            # the untouched crown and nape. Small layer spacing retains volume.
            layer=.0035+.001*((strand_index*73)%101)/100.
            changes=[Vector() for p in strand]
            for j,p in enumerate(strand[1:],1):
                weight=(smoothstep(.033,.052,abs(p.x))*smoothstep(1.19,1.215,p.y)
                        *(1.-smoothstep(1.298,1.325,p.y))*smoothstep(-.045,-.015,p.z))
                if weight<1e-5:continue
                hit,n,_,gap=self.bvh.find_nearest(p)
                if hit is None or gap>.045:continue
                if n.dot(hit-Vector(self.surface.center))<0:n=-n
                signed=(p-hit).dot(n)
                if signed>layer:
                    target=hit+(p-hit).normalized()*layer
                    changes[j]=(target-p)*weight*min(1.,j/2.)
            # Smooth displacements, not strand positions: preserve curl shape.
            for j in range(1,len(strand)):
                delta=(changes[max(1,j-1)]+changes[j]*2+changes[min(len(strand)-1,j+1)])*.25
                strand[j]+=delta
                if delta.length>1e-6:
                    self.seated_points+=1;self.max_seat=max(self.max_seat,delta.length)
        if region in (0,3,4):
            # Seating/smoothing can pull a chord through a convex scalp even
            # when the original endpoints were clear. Recheck final geometry.
            for attempt in range(3):
                for j,p in enumerate(strand[1:],1):
                    delta=self.head_clearance(p,.0035 if region==0 else .0018)
                    if delta.length>1e-7:strand[j]+=delta;self.head_repairs+=1
                for j in range(len(strand)-1):
                    a,b=strand[j],strand[j+1]
                    for t in (.25,.5,.75):
                        delta=self.head_clearance(a.lerp(b,t),(.0031 if region==0 else .0014)*(t if j==0 else 1.))
                        if delta.length<=1e-7:continue
                        if j==0:b+=delta/t
                        else:
                            denom=(1-t)**2+t*t;a+=delta*((1-t)/denom);b+=delta*(t/denom)
                        self.head_repairs+=1
                    strand[j],strand[j+1]=a,b
            # Guarantee that the first edge exits the skin instead of grazing
            # back through it after Jolt's barycentric root projection.
            root,n,_,_=self.bvh.find_nearest(strand[0])
            if n.dot(root-Vector(self.surface.center))<0:n=-n
            inward=(strand[1]-strand[0]).dot(n)
            if inward<.0007:
                strand[1]+=n*(.0007-inward)
                strand[2]+=n*(.0007-inward)*.25
                self.head_repairs+=1
        ear_margin=.004 if region==2 else .003 if region==0 else .002
        edge_margin=ear_margin-.0005
        for j,p in enumerate(strand[1:],1):
            for ear in self.ears:
                if ear.near(p):strand[j]=p=ear.resolve(p,ear_margin)
        # Shared endpoints can affect the preceding edge. Iterate both orders
        # to closure and then explicitly verify every edge; no hidden trimming.
        for attempt in range(24):
            changed=False
            order=range(len(strand)-1) if attempt%2==0 else range(len(strand)-2,-1,-1)
            for j in order:
                for ear in self.ears:
                    a,b,repair=ear.detour_segment(strand[j],strand[j+1],j==0,edge_margin)
                    if repair:
                        strand[j],strand[j+1]=a,b;changed=True;self.segment_repairs+=1
            if not changed:break
        for j in range(len(strand)-1):
            for ear in self.ears:
                if ear.segment_overlap(strand[j],strand[j+1],edge_margin-.0001) is not None:
                    raise ValueError(f'Unresolved ear-crossing segment {strand_index}:{j}')
        return strand
    def record(self):
        return {'supraAuricularRootRepairs':self.root_repairs,'maxRootRepairMetres':self.max_root,
                'seatedPoints':self.seated_points,'maxSeatingMetres':self.max_seat,
                'continuousSegmentRepairs':self.segment_repairs,'unresolvedEarSegments':0,
                'finalScalpPointAndChordRepairs':self.head_repairs,
                'earSegmentClearanceMetres':{'cap':.0024,'tail':.0034,'faceAndWisps':.0014},'capLayerGapMetres':[.0035,.0045]}
