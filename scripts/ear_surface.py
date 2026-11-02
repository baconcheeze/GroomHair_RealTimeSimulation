"""Small offline ear envelopes from the actual character's head vertices.
Blender-only authoring helper, not a simulation or runtime collision solver.
"""
import bmesh
import struct,math
from mathutils import Vector,Quaternion

def read_head_bind(path):
    """The same stored frame-0 Head transform used by saveState's bind samples.
    The playground's initial world transform includes floor/yaw, so it is NOT
    this transform. Keep render and GPU-skinned scalp in the same coordinates.
    """
    b=path.read_bytes();head,nv=struct.unpack_from('<2I',b,0);off=8+nv*12
    nt=struct.unpack_from('<I',b,off)[0];off+=4+nt*12
    nj=struct.unpack_from('<I',b,off)[0];off+=4+nj*64
    nw=struct.unpack_from('<I',b,off)[0];off+=4+nv*nw*8
    nf=struct.unpack_from('<I',b,off)[0];off+=4
    assert head<nj and nf>0
    tx,ty,tz,x,y,z=struct.unpack_from('<6f',b,off+head*24)
    q=Quaternion((math.sqrt(max(0.,1.-x*x-y*y-z*z)),x,y,z))
    m=q.to_matrix().to_4x4();m.translation=Vector((tx,ty,tz));return m

class EarEnvelope:
    def __init__(self, vertices, side, max_y=None):
        candidates=[Vector(p) for p in vertices[:13320]
                    if p[0]*side>.055 and 1.21<p[1]<(1.28 if max_y is None else max_y) and -.015<p[2]<.055]
        seed=max(candidates,key=lambda p:p.x*side)
        samples=[p for p in candidates if p.x*side>seed.x*side-.014
                 and abs(p.y-seed.y)<.028 and abs(p.z-seed.z)<.026]
        assert len(samples)>30
        bm=bmesh.new();vs=[bm.verts.new(p) for p in samples]
        bmesh.ops.convex_hull(bm,input=vs,use_existing_faces=False)
        bm.normal_update();faces=list(bm.faces)
        used=set(v for f in faces for v in f.verts)
        self.points=sorted((v.co.copy() for v in used),key=lambda p:tuple(p))
        self.center=sum(self.points,Vector())/len(self.points)
        self.planes=[]
        for f in faces:
            normal=f.normal.normalized()
            if normal.dot(f.calc_center_median()-self.center)<0:normal=-normal
            self.planes.append((normal,normal.dot(f.verts[0].co)))
        self.side=side;bm.free()
        self.bounds=([min(p[k] for p in self.points) for k in range(3)],
                     [max(p[k] for p in self.points) for k in range(3)])
    def near(self,p):
        return (p.x*self.side>.045 and abs(p.y-self.center.y)<.050
                and abs(p.z-self.center.z)<.060)
    def separation(self,p):
        return max(((n.dot(p)-d,n) for n,d in self.planes),key=lambda x:x[0])
    def resolve(self,p,clearance=.002):
        distance,normal=self.separation(p)
        return p+normal*max(0,clearance-distance)
    def segment_overlap(self,a,b,clearance=.0015):
        """Exact interval inside all offset convex planes, not vertex sampling."""
        lo,hi=self.bounds
        if any(max(a[k],b[k])<lo[k]-clearance or min(a[k],b[k])>hi[k]+clearance for k in range(3)):return None
        enter,leave=0.,1.
        for n,d in self.planes:
            start=n.dot(a)-d-clearance;slope=n.dot(b-a)
            if abs(slope)<1e-12:
                if start>0:return None
            elif slope>0:leave=min(leave,-start/slope)
            else:enter=max(enter,-start/slope)
            if enter>=leave-1e-7:return None
        return (enter,leave) if enter<leave-1e-7 else None
    def detour_segment(self,a,b,pin_a=False,clearance=.0015):
        if self.segment_overlap(a,b,clearance) is None:return a,b,False
        # Put the complete edge on one exterior half-space. Moving endpoints
        # independently to different faces can still send the chord through an ear.
        choices=[]
        for n,d in self.planes:
            da=max(0.,d+clearance-n.dot(a));db=max(0.,d+clearance-n.dot(b))
            if pin_a and da>1e-7:continue
            choices.append((da*da+db*db,da,db,n))
        if not choices:raise ValueError('Hair root lies inside ear envelope; repair scalp attachment first')
        _,da,db,n=min(choices,key=lambda c:c[0])
        return a+n*da,b+n*db,True
    def record(self):
        return {'side':self.side,'center':list(self.center),'vertices':[list(p) for p in self.points],
                'bounds':[[min(p[k] for p in self.points) for k in range(3)],[max(p[k] for p in self.points) for k in range(3)]],
                'planes':[[*n,d] for n,d in self.planes]}
