"""Blender offline audit of exact staged and app-captured strand geometry."""
import bpy,json,struct,sys
from pathlib import Path
from mathutils import Vector
R=Path(__file__).resolve().parents[1];sys.path.insert(0,str(R/'scripts'))
relative=sys.argv[sys.argv.index('--')+1] if '--' in sys.argv else 'artifacts/metahuman-finish/v6/ear-right'
from groom_surface import ScalpSurface
from ear_surface import EarEnvelope
surface=ScalpSurface(R/'assets/moca/character.bin');ears=[EarEnvelope(surface.v,s) for s in (-1,1)]
data=(R/'assets/moca/metahuman.hair').read_bytes();positions=[Vector(p) for p in struct.iter_unpack('<3f',data[128:])]
meta=json.loads((R/'assets/moca/metahuman.json').read_text())
def score(samples):
    groups={'roots':[],'free':[]}
    for index,p in samples:
        for ear in ears:
            if not ear.near(p):continue
            d,n=ear.separation(p)
            if d<-.00001:groups['roots' if index%32==0 else 'free'].append((d,index,list(p)))
    return {k:{'count':len(v),'worst':sorted(v)[:12]} for k,v in groups.items()}
out={'staged':score(enumerate(positions))}
for path in sorted((R/relative).glob('step-*.json')):
    s=json.loads(path.read_text());out[path.stem]=score((int(a[0]),Vector(a[1:])) for a in s['earRegionRenderBindSamples'])
    if s['step']!=0:continue
    delta=[]
    for index,*p in s['earRegionRenderBindSamples']:
        d=(Vector(p)-positions[index]).length
        if d>.00001:delta.append((d,index,list(p),list(positions[index])))
    out['stagedVsRuntime']={'different':len(delta),'worst':sorted(delta,reverse=True)[:12]}
(R/relative/'ear-audit.json').write_text(json.dumps(out,indent=2))
print(json.dumps(out,indent=2))
