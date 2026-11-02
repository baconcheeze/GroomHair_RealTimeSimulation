import bpy,json,sys
from pathlib import Path
from mathutils import Vector,Matrix
from mathutils.bvhtree import BVHTree
R=Path(__file__).resolve().parents[1];sys.path.insert(0,str(R/'scripts'))
from groom_surface import ScalpSurface
s=ScalpSurface(R/'assets/moca/character.bin')
def triangles(n):return n[2] if n[3] is None else triangles(n[2])+triangles(n[3])
bvh=BVHTree.FromPolygons(s.v,triangles(s.tree),all_triangles=True)
p=R/'artifacts/metahuman-finish/final-close/ear-right/step-0180.json'
state=json.loads(p.read_text());center=Vector(s.center);bad=[]
for index,*xyz in state['earRegionRenderBindSamples']:
    if index%32==0:continue
    v=Vector(xyz);direction=(v-center).normalized();hit=bvh.ray_cast(center,direction,.25)[0]
    if hit is None:continue
    depth=(hit-center).length-(v-center).length
    if depth>.00001:bad.append((depth,index,xyz))
bad.sort(reverse=True)
o={'insidePoints':len(bad),'worst':bad[:24],'depthQuantilesMetres':{str(q):bad[min(len(bad)-1,int(q*len(bad)))][0] for q in (0,.1,.5,.9)} if bad else {}}
p.with_name('head-depth.json').write_text(json.dumps(o,indent=2));print(json.dumps(o,indent=2))
