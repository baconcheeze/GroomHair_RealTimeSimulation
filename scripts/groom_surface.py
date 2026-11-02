"""Small offline ray surface for authoring on the staged, repaired head mesh."""
import json,math,struct
def sub(a,b):return tuple(a[k]-b[k] for k in range(3))
def cross(a,b):return (a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0])
def dot(a,b):return sum(x*y for x,y in zip(a,b))
class ScalpSurface:
    def __init__(self,path):
        b=path.read_bytes();nv=struct.unpack_from('<I',b,4)[0]
        patch=json.loads(path.with_name('scalp-patch.json').read_text())
        assert patch['vertices']==nv
        patch_start=nv-patch['addedVertices']
        self.v=[struct.unpack_from('<3f',b,8+i*12) for i in range(nv)]
        nt=struct.unpack_from('<I',b,8+nv*12)[0];off=12+nv*12
        ts=[struct.unpack_from('<3I',b,off+i*12) for i in range(nt)]
        self.head_triangles_all=[t for t in ts if all(i<13320 or i>=patch_start for i in t)]
        # Upper skull only: original face/scalp vertices and added scalp patch.
        ts=[t for t in ts if all(i<13320 or i>=patch_start for i in t) and min(self.v[i][1] for i in t)>1.18]
        assert ts
        def tree(ts):
            lo=tuple(min(self.v[i][k] for t in ts for i in t) for k in range(3));hi=tuple(max(self.v[i][k] for t in ts for i in t) for k in range(3))
            if len(ts)<12:return lo,hi,ts,None
            axis=max(range(3),key=lambda k:hi[k]-lo[k]);ts.sort(key=lambda t:sum(self.v[i][axis] for i in t))
            mid=len(ts)//2;return lo,hi,tree(ts[:mid]),tree(ts[mid:])
        self.tree=tree(ts);self.center=(0,1.270,.022)
    def ray(self,theta,phi,outer=False):
        d=(math.sin(theta)*math.sin(phi),math.cos(theta),-math.sin(theta)*math.cos(phi))
        return self.cast(self.center,d,outer)
    def front(self,x,y):
        """Front-facing head intersection at an authored forehead x/y."""
        return self.cast((x,y,self.center[2]),(0,0,-1))
    def cast(self,origin,d,outer=False):
        best=0. if outer else 1.
        def visit(node):
            nonlocal best
            lo,hi,a,b=node;near=0;far=1. if outer else best
            for k in range(3):
                if abs(d[k])<1e-14:
                    if not lo[k]<=origin[k]<=hi[k]:return
                else:
                    x=(lo[k]-origin[k])/d[k];y=(hi[k]-origin[k])/d[k]
                    near=max(near,min(x,y));far=min(far,max(x,y))
                    if far<near:return
            if b is not None:visit(a);visit(b);return
            for t in a:
                p=self.v[t[0]];e=sub(self.v[t[1]],p);f=sub(self.v[t[2]],p);h=cross(d,f);det=dot(e,h)
                if abs(det)<1e-14:continue
                s=sub(origin,p);u=dot(s,h)/det
                if u<0 or u>1:continue
                c=cross(s,e);v=dot(d,c)/det
                if v<0 or u+v>1:continue
                length=dot(f,c)/det
                if 0<length<1 and ((outer and length>best) or (not outer and length<best)):best=length
        visit(self.tree)
        if best>=1 or best<=0:raise ValueError(f'No repaired scalp intersection origin={origin}, direction={d}')
        return tuple(origin[k]+d[k]*best for k in range(3))
