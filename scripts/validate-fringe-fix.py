"""Verify delivered defaults, real fringe motion, finite state and reset."""
import hashlib,json,math,subprocess
from pathlib import Path
R=Path(__file__).resolve().parents[1];O=R/'artifacts/fringe-diagnosis/final';O.mkdir(parents=True,exist_ok=True)
runs=[]
for config,name,args in [
 ('Release','moving',['--steps','240','--capture-every','15','--reset-check']),
 ('Release','static',['--steps','240','--capture-every','15','--no-animation']),
 ('Debug','moving',['--steps','120','--capture-every','15','--reset-check']),
 ('Release','controls',['--control-check'])]:
    out=O/config/name;exe=R/f'build/jolt-only/bin/{config}/HairLabDX12.exe'
    cmd=[str(exe),*args,'--capture-dir',str(out),'--log',str(out/'app.log')]
    p=subprocess.Popen(cmd,cwd=R)
    try:code=p.wait(timeout=90)
    except subprocess.TimeoutExpired:p.terminate();p.wait(timeout=10);raise RuntimeError('Own final fringe check timed out')
    runs.append({'command':cmd,'exit':code,'exeSha256':hashlib.sha256(exe.read_bytes()).hexdigest()})
    (O/'runs.json').write_text(json.dumps(runs,indent=2));print(config,name,code,flush=True);assert code==0
def load(folder):return {s['step']:s for s in [json.loads(p.read_text()) for p in folder.glob('step-*.json')]}
result={}
for config in ('Release','Debug'):
    p=O/config/'moving';ss=load(p);a=ss[0]
    gp=a['materials'][1]['globalPose'];assert abs(gp[0]-.01)<1e-7 and abs(gp[1]-.001)<1e-7
    ids=[i for i,m in enumerate(a['guideMaterials']) if m==1]
    assert len(ids)==320 and all(a['guideFreeVertices'][i]==31 for i in ids)
    assert all(s['finite'] and s['maxRootAttachmentErrorMeters']<1e-5 for s in ss.values())
    assert json.loads((p/'reset-0000.json').read_text())==a
    assert (p/'reset-0000.png').read_bytes()==(p/'step-0000.png').read_bytes()
    result[config]={'actualFringeMaterial':a['materials'][1],'guideCount':len(ids),'freeVerticesPerGuide':31,'finite':True,'exactReset':True}
moving=load(O/'Release/moving');static=load(O/'Release/static');ids=[i for i,m in enumerate(moving[0]['guideMaterials']) if m==1]
curve={}
for k,s in moving.items():
    d=[math.dist(s['guideTips'][i],static[k]['guideTips'][i]) for i in ids]
    curve[k]={'rmsMillimetres':1000*math.sqrt(sum(x*x for x in d)/len(d)),'maxMillimetres':1000*max(d)}
assert max(v['rmsMillimetres'] for v in curve.values())>1.,'Head-animation response remains visually tiny'
assert max(v['rmsMillimetres'] for k,v in curve.items() if k>=120)>1.,'Only startup transient produced visible response'
result['animationInducedCurve']=curve
out=O/'Release/controls'
for prefix in ('idle','camera'):assert (out/(prefix+'-before.json')).read_bytes()==(out/(prefix+'-after.json')).read_bytes()
result['controllerAPI']='pause / step / reset / style switch / camera isolation passed; no physical mouse test'
(O/'validation.json').write_text(json.dumps(result,indent=2));print(json.dumps(curve,indent=2))
