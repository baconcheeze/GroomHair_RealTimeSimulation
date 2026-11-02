"""Read-only original curve endpoints versus current authored groom."""
import bpy,json,struct,math,sys
import numpy as np
from pathlib import Path
from array import array
from mathutils import Vector
R=Path(__file__).resolve().parents[1]
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.wm.alembic_import(filepath=str(R/'assets/MetaHuman/long_wavy_twin_ponytail_hair/Long_Wavy_Twin_Ponytail_Hair.abc'),as_background_job=False)
meta=json.loads((R/'assets/moca/metahuman.json').read_text());current=np.frombuffer((R/'assets/moca/metahuman.hair').read_bytes(),dtype='<f4',offset=128).reshape(-1,32,3).astype(float)
out=[]
for g in meta['groups']:
    o=bpy.data.objects[g['sourceObject']];coords=[]
    for curve in o.data.curves:
        p=o.matrix_world@curve.points[0].position;t=o.matrix_world@curve.points[-1].position
        coords.append([(p.x*.01,1.27+(p.z-160.56)*.0091,.022+(p.y-.5)*.0085),(t.x*.01,1.27+(t.z-160.56)*.0091,.022+(t.y-.5)*.0085)])
    a=np.array(coords);b=current[g['firstStrand']:g['firstStrand']+g['strandCount']]
    edges=np.diff(b,axis=1);l=np.linalg.norm(edges,axis=2);dot=np.sum(edges[:,:-1]*edges[:,1:],axis=2)/(l[:,:-1]*l[:,1:]);angles=np.arccos(np.clip(dot,-1,1))*180/math.pi
    out.append({'object':o.name,'region':g['region'],'count':len(a),'sourceRootMean':a[:,0].mean(axis=0).tolist(),'sourceRootBounds':[a[:,0].min(axis=0).tolist(),a[:,0].max(axis=0).tolist()],'sourceTipMean':a[:,1].mean(axis=0).tolist(),'stagedRootMean':b[:,0].mean(axis=0).tolist(),'stagedTipMean':b[:,-1].mean(axis=0).tolist(),'maxAdjacentAngleDeg':float(angles.max()),'angleQuantiles':np.quantile(angles,[.5,.95,.99,.999]).tolist(),'maxSegmentRatio':float((l.max(axis=1)/l.mean(axis=1)).max())})
name=sys.argv[sys.argv.index('--')+1] if '--' in sys.argv else 'shape-audit-current.json'
(R/'artifacts/hair-salon-finish'/name).write_text(json.dumps(out,indent=2));print(json.dumps(out,indent=2))
