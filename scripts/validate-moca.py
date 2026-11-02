"""Bounded content/rig/Jolt integration checks, no custom-solver comparisons."""
import hashlib,json,math,struct,subprocess,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'artifacts/moca/final';OUT.mkdir(parents=True,exist_ok=True)
runs=[]
if '--evaluate-only' not in sys.argv:
    for config,name,options in [
        ('Release','motion',['--steps','120','--capture-every','15','--reset-check']),
        ('Debug','motion',['--steps','60','--capture-every','15','--reset-check','--guides','--hulls']),
        ('Release','controls',['--control-check']),('Debug','controls',['--control-check']),
        ('Release','rig',['--paused','--frames','3','--bones','--hulls','--capture-ui']),
        ('Release','side',['--steps','30','--capture-every','30','--view','side']),
    ]:
        target=OUT/config/name;exe=ROOT/f'build/jolt-only/bin/{config}/HairLabDX12.exe'
        command=[str(exe),*options,'--capture-dir',str(target),'--log',str(target/'app.log')]
        p=subprocess.Popen(command,cwd=ROOT)
        try:code=p.wait(timeout=90)
        except subprocess.TimeoutExpired:
            p.terminate();p.wait(timeout=10);raise RuntimeError(f'Timed out own validation process {p.pid}')
        runs.append(dict(configuration=config,name=name,command=command,exit=code,sha256=hashlib.sha256(exe.read_bytes()).hexdigest()))
        (OUT/'runs.json').write_text(json.dumps(runs,indent=2));print(config,name,code,flush=True)
        if code:raise RuntimeError(f'Failed app test {target}')
results={}
for config in ('Release','Debug'):
    path=OUT/config/'motion';states=[json.loads(p.read_text()) for p in sorted(path.glob('step-*.json'))]
    first=states[0];assert first['character']=='Moca' and first['guides']==1600 and first['render']==16000 and first['hulls']==3
    assert all(s['finite'] and s['maxRootAttachmentErrorMeters']<1e-5 for s in states)
    metrics={}
    for key in ('guideTips','renderTips','scalpVertices','hullWorldColumns'):
        curves=[[math.dist(a,b) for a,b in zip(first[key],s[key])] for s in states]
        metrics[key]={'peakChange':max(max(c) for c in curves),'peakRmsChange':max(math.sqrt(sum(v*v for v in c)/len(c)) for c in curves)}
        assert metrics[key]['peakChange']>0,f'No motion: {key}'
    reset=json.loads((path/'reset-0000.json').read_text());assert reset==first
    assert (path/'reset-0000.png').read_bytes()==(path/'step-0000.png').read_bytes()
    metrics['exactReset']=True;metrics['maxRootAttachmentErrorMeters']=max(s['maxRootAttachmentErrorMeters'] for s in states)
    control=OUT/config/'controls'
    assert (control/'idle-before.json').read_bytes()==(control/'idle-after.json').read_bytes()
    assert (control/'camera-before.json').read_bytes()==(control/'camera-after.json').read_bytes()
    assert json.loads((control/'single-step.json').read_text())['step']==1
    metrics['controllerAPI']='pause / step / run-pause / reset / camera-state isolation passed'
    results[config]=metrics
# Rig import is deliberately separate from GPU/physics results.
manifest=json.loads((ROOT/'assets/moca/manifest.json').read_text(encoding='utf-8'))
rig=json.loads((ROOT/'assets/moca/rig.json').read_text(encoding='utf-8'))
assert len(rig['bones'])==315 and rig['headJoint']==4
assert len({b['name'] for b in rig['bones']})==315
assert manifest['bindIdentityMaxError']<1e-5
for name,digest in manifest['files'].items():
    assert hashlib.sha256((ROOT/'assets/moca'/name).read_bytes()).hexdigest()==digest,name
results['rig']={'bones':315,'preservedNamesAndHierarchy':True,'bindIdentityMaxError':manifest['bindIdentityMaxError'],'sourceClips':0,'authoredAnimationFrames':240}
results['unverified']='Physical mouse / keyboard / resize interaction not exercised by these API tests.'
(OUT/'validation.json').write_text(json.dumps(results,indent=2));print(json.dumps(results,indent=2))
