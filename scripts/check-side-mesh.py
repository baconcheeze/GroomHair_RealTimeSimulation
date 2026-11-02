"""Explicit offline render-edge versus actual head triangles, in bind space."""
import bpy,json,sys
from pathlib import Path
from mathutils import Vector
from mathutils.bvhtree import BVHTree
R=Path(__file__).resolve().parents[1];sys.path.insert(0,str(R/'scripts'))
from groom_surface import ScalpSurface
from ear_surface import read_head_bind
surface=ScalpSurface(R/'assets/moca/character.bin')
def triangles(node):return node[2] if node[3] is None else triangles(node[2])+triangles(node[3])
bvh=BVHTree.FromPolygons(surface.v,triangles(surface.tree),all_triangles=True)
folder=R/(sys.argv[sys.argv.index('--')+1] if '--' in sys.argv else 'artifacts/hair-salon-finish/verified/Release/walk')
results=[]
bind=read_head_bind(R/'assets/moca/character.bin')
for path in sorted(folder.glob('step-*.json')):
    s=json.loads(path.read_text());samples=s['earRegionRenderBindSamples'];hits=[];checked=0
    posed=[bind@Vector(v) for v in s['scalpVertices']]
    bvh=BVHTree.FromPolygons(posed,triangles(surface.tree),all_triangles=True)
    for previous,current in zip(samples,samples[1:]):
        index=current[0]
        if index!=previous[0]+1 or index%32==0:continue
        a,b=Vector(previous[1:]),Vector(current[1:]);edge=b-a;length=edge.length
        if length<.00008:continue
        direction=edge/length
        hit,n,triangle,t=bvh.ray_cast(a+direction*.00003,direction,length-.00006)
        checked+=1
        if hit is not None:hits.append({'endVertex':index,'position':list(hit),'fraction':(t+.00003)/length,
                                      'distanceFromSegmentStartMetres':t+.00003,
                                      'follicleContact':index%32==1 and t+.00003<.0002})
    max_deform=max((posed[i]-Vector(surface.v[i])).length for i in range(13320) if 1.20<surface.v[i][1]<1.31)
    results.append({'capture':path.name,'checkedSegments':checked,'crossings':len(hits),'follicleContacts':sum(h['follicleContact'] for h in hits),'crossingsAwayFromFollicles':sum(not h['follicleContact'] for h in hits),'firstCrossings':hits[:24],'maxBindHeadMeshDifferenceMetres':max_deform})
    print(path.name,checked,len(hits),'away from follicle',sum(not h['follicleContact'] for h in hits),flush=True)
(folder/'mesh-crossings.json').write_text(json.dumps({'qualification':'Captured contiguous side-region render edges against actual GPU-skinned head/scalp triangles in the SAME hair-local frame, transformed together to bind-world for inspection; 30um endpoint exclusion prevents self-contact roundoff. Not an arbitrary-pose proof.','results':results},indent=2))
