"""Blender's bundled numpy: fast, explicit point/edge ear clearance scoring.
Does not deform captures or supply runtime simulation corrections.
"""
import bpy,json,sys,hashlib
import numpy as np
from pathlib import Path
R=Path(__file__).resolve().parents[1]
folder=R/(sys.argv[sys.argv.index('--')+1] if '--' in sys.argv else 'artifacts/hair-salon-finish/verified')
meta=json.loads((R/'assets/moca/metahuman.json').read_text())
materials=np.frombuffer((R/'assets/moca/metahuman.materials').read_bytes(),dtype='<u4',offset=4)
results=[]
def score(ids,p):
    regions={str(i):{'rootsInside':0,'freeInside':0,'maxRootDepthMetres':0.,'maxFreeDepthMetres':0.,'crossingEdges':0,'worstVertex':None} for i in set(materials.tolist())}
    for ear in meta['earFit']['geometry']:
        lo,hi=np.array(ear['bounds']);planes=np.array(ear['planes']);n,d=planes[:,:3],planes[:,3]
        mask=np.all((p>=lo-.0001)&(p<=hi+.0001),axis=1)
        chosen=np.flatnonzero(mask)
        for start in range(0,len(chosen),2048):
            batch=chosen[start:start+2048];sep=np.max(p[batch]@n.T-d,axis=1)
            for k in np.flatnonzero(sep<-.00001):
                index=int(ids[batch[k]]);reg=regions[str(int(materials[index//32]))];root=index%32==0
                reg['rootsInside' if root else 'freeInside']+=1
                key='maxRootDepthMetres' if root else 'maxFreeDepthMetres'
                if -sep[k]>reg[key]:reg[key]=float(-sep[k]);reg['worstVertex']=index
        contiguous=(ids[1:]==ids[:-1]+1)&(ids[1:]%32!=0)
        a,b=p[:-1],p[1:]
        selected=np.flatnonzero(contiguous&np.all((np.maximum(a,b)>=lo)&(np.minimum(a,b)<=hi),axis=1))
        for start in range(0,len(selected),2048):
            batch=selected[start:start+2048];sa=a[batch]@n.T-d;sb=b[batch]@n.T-d;slope=sb-sa
            parallel=np.abs(slope)<1e-12
            safe=np.where(parallel,1.,slope);t=-sa/safe
            enter=np.maximum(0.,np.max(np.where(slope< -1e-12,t,-np.inf),axis=1))
            leave=np.minimum(1.,np.min(np.where(slope>1e-12,t,np.inf),axis=1))
            intersects=(leave>enter+1e-5)&~np.any(parallel&(sa>-.00001),axis=1)
            for index in ids[batch[intersects]]:regions[str(int(materials[int(index)//32]))]['crossingEdges']+=1
    return regions
data=np.frombuffer((R/'assets/moca/metahuman.hair').read_bytes(),dtype='<f4',offset=128).reshape(-1,3).astype(np.float64)
staged=score(np.arange(len(data)),data)
for path in sorted(folder.rglob('step-*.json')):
    s=json.loads(path.read_text())
    if s['hairstyle']!='metahuman':continue
    samples=np.array(s['earRegionRenderBindSamples']);ids=samples[:,0].astype(np.int64);p=samples[:,1:]
    result={'capture':str(path.relative_to(R)),'step':s['step'],'regions':score(ids,p)}
    if s['step']==0:result['maxStagedPositionDifferenceMetres']=float(np.max(np.linalg.norm(p-data[ids],axis=1)))
    results.append(result)
report={'profile':meta['profile'],'staged':staged,'results':results,'qualification':'Actual rendered points and contiguous captured edges versus measured ear convex envelopes; depth threshold 10um excludes float surface noise. Roots kept separately. This does not certify every untested pose.','inputSha256':hashlib.sha256((R/'assets/moca/metahuman.hair').read_bytes()).hexdigest()}
(folder/'clearance.json').write_text(json.dumps(report,indent=2))
for x in results:
    print(x['capture'],sum(v['freeInside'] for v in x['regions'].values()),max(v['maxFreeDepthMetres'] for v in x['regions'].values()),'edges',sum(v['crossingEdges'] for v in x['regions'].values()),flush=True)
