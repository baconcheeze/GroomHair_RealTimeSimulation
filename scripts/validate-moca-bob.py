"""Bounded bob/long style integration checks. No solver research tests."""
import hashlib,json,math,struct,subprocess
from pathlib import Path
R=Path(__file__).resolve().parents[1]
groom=json.loads((R/'assets/moca/bob.json').read_text(encoding='utf-8'));refit='scalpSha256' in groom
folder='artifacts/moca-bob-volume/final' if groom.get('revision')=='flow-v3-volume' else ('artifacts/moca-bob-redesign/validated' if groom.get('revision')=='flow-v2' else ('artifacts/moca-bob-refit/final' if refit else 'artifacts/moca-bob/final'))
O=R/folder;O.mkdir(parents=True,exist_ok=True)
runs=[]
for config,name,args in [
 ('Release','bob',['--steps','120','--capture-every','15','--reset-check']),
 ('Debug','bob',['--steps','60','--capture-every','15','--reset-check']),
 ('Release','controls',['--control-check']),('Debug','controls',['--control-check']),
 ('Release','long',['--hairstyle','long','--steps','30','--capture-every','15','--reset-check']),
 ('Release','side',['--view','side','--steps','30','--capture-every','30']),
 ('Release','back',['--view','back','--steps','60','--capture-every','30','--reset-check']),
 ('Release','ui',['--paused','--frames','3','--capture-ui'])]:
    exe=R/f'build/jolt-only/bin/{config}/HairLabDX12.exe';out=O/config/name
    cmd=[str(exe),*args,'--capture-dir',str(out),'--log',str(out/'app.log')]
    p=subprocess.Popen(cmd,cwd=R)
    try:code=p.wait(timeout=60)
    except subprocess.TimeoutExpired:
        p.terminate();p.wait(timeout=10);raise RuntimeError('Own bounded test timed out')
    runs.append({'command':cmd,'exit':code,'exeSha256':hashlib.sha256(exe.read_bytes()).hexdigest()})
    (O/'runs.json').write_text(json.dumps(runs,indent=2));print(config,name,code,flush=True)
    assert code==0
results={}
for config,name in [('Release','bob'),('Debug','bob'),('Release','long')]:
    out=O/config/name;ss=[json.loads(p.read_text()) for p in sorted(out.glob('step-*.json'))];a=ss[0]
    assert a['hairstyle']==name and a['guides']==1600 and a['render']==16000
    assert all(s['finite'] and s['maxRootAttachmentErrorMeters']<1e-5 for s in ss)
    metrics={}
    for key in ('guideTips','renderTips'):
        metrics[key+'PeakRmsChange']=max(math.sqrt(sum(math.dist(x,y)**2 for x,y in zip(a[key],b[key]))/len(a[key])) for b in ss)
        assert metrics[key+'PeakRmsChange']>1e-5
    assert json.loads((out/'reset-0000.json').read_text())==a
    assert (out/'step-0000.png').read_bytes()==(out/'reset-0000.png').read_bytes()
    metrics['maxRootAttachmentErrorMeters']=max(s['maxRootAttachmentErrorMeters'] for s in ss)
    metrics['exactReset']=True;results[config+'/'+name]=metrics
for config in ('Release','Debug'):
    out=O/config/'controls'
    for prefix in ('idle','camera'):
        assert (out/(prefix+'-before.json')).read_bytes()==(out/(prefix+'-after.json')).read_bytes()
    assert json.loads((out/'style-switched.json').read_text())['hairstyle']=='long'
    assert json.loads((out/'control-reset.json').read_text())['hairstyle']=='bob'
results['styleControllerAPI']='bob -> long -> bob; pause / step / reset and camera-state isolation passed'
long=(R/'assets/moca/studio.hair').read_bytes();bob=(R/'assets/moca/bob.hair').read_bytes()
assert hashlib.sha256(long).hexdigest()=='d0280506307d870c3ddc569f84efc2b31035701c7e00d49e33dd123fddfe3136'
assert len(long)==len(bob)
if refit:
    from groom_surface import ScalpSurface
    scalp=R/'assets/moca/character.bin';assert hashlib.sha256(scalp.read_bytes()).hexdigest()==groom['scalpSha256']
    surface=ScalpSurface(scalp);error=0.
    for i in range(16000):
        root=struct.unpack_from('<3f',bob,128+i*32*12);x,y,z=[root[k]-surface.center[k] for k in range(3)]
        theta=math.atan2(math.hypot(x,z),y);phi=math.atan2(x,-z)
        error=max(error,math.dist(root,surface.ray(theta,phi)))
    assert error<1e-6,error
    results['all16000RootsOnRepairedScalpMaxErrorMeters']=error
else:
    assert all(long[128+i*32*12:128+i*32*12+12]==bob[128+i*32*12:128+i*32*12+12] for i in range(16000))
    results['scalpRootsBitExact']=True
results['longGroomPreserved']=True
results['manualInteraction']='Not exercised; controller/API and actual scene captures only'
(O/'validation.json').write_text(json.dumps(results,indent=2));print(json.dumps(results,indent=2))
