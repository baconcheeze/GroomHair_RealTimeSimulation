"""Bounded editorial content, skin attachment and live-Jolt render checks."""
import hashlib,json,math,struct,subprocess
from pathlib import Path
from groom_surface import ScalpSurface
R=Path(__file__).resolve().parents[1];O=R/'artifacts/uniform-hair/final';O.mkdir(parents=True,exist_ok=True)
runs=[]
for config,name,args in [
 ('Release','beauty',['--frames','3']),
 ('Release','motion',['--view','hair','--steps','120','--capture-every','15','--reset-check']),
 ('Debug','motion',['--view','hair','--steps','60','--capture-every','30','--reset-check']),
 ('Release','side',['--view','side','--steps','60','--capture-every','30']),
 ('Release','back',['--view','back','--steps','60','--capture-every','30']),
 ('Release','controls',['--control-check']),
 ('Debug','controls',['--control-check']),
 ('Release','long',['--hairstyle','long','--view','hair','--steps','15','--capture-every','15'])]:
    exe=R/f'build/jolt-only/bin/{config}/HairLabDX12.exe';p=O/config/name
    command=[str(exe),*args,'--capture-dir',str(p),'--log',str(p/'app.log')]
    process=subprocess.Popen(command,cwd=R)
    try:code=process.wait(timeout=120)
    except subprocess.TimeoutExpired:process.terminate();process.wait(timeout=10);raise RuntimeError('Own bounded editorial check timed out')
    runs.append({'configuration':config,'name':name,'exit':code,'command':command,'exeSha256':hashlib.sha256(exe.read_bytes()).hexdigest()})
    (O/'runs.json').write_text(json.dumps(runs,indent=2));print(config,name,code,flush=True);assert code==0
results={}
for config in ('Release','Debug'):
    p=O/config/'motion';ss=[json.loads(f.read_text()) for f in sorted(p.glob('step-*.json'))];a=ss[0]
    assert all(t['materials'][0]==t['materials'][1] for t in ss)
    assert a['guides']==1600 and a['render']==32000
    assert len(a['renderTips'])==32000 and all(s['finite'] and s['maxRootAttachmentErrorMeters']<1e-5 for s in ss)
    stats={}
    for key in ('guideTips','renderTips'):
        stats[key+'PeakRmsMetres']=max(math.sqrt(sum(math.dist(x,y)**2 for x,y in zip(a[key],s[key]))/len(a[key])) for s in ss)
        assert stats[key+'PeakRmsMetres']>1e-5
    stats['maxRootAttachmentErrorMetres']=max(s['maxRootAttachmentErrorMeters'] for s in ss)
    assert json.loads((p/'reset-0000.json').read_text())==a
    assert (p/'reset-0000.png').read_bytes()==(p/'step-0000.png').read_bytes()
    stats['exactReset']=True;results[config]=stats
    p=O/config/'controls'
    for prefix in ('idle','camera'):assert (p/(prefix+'-before.json')).read_bytes()==(p/(prefix+'-after.json')).read_bytes()
    assert json.loads((p/'style-switched.json').read_text())['render']==16000
    assert json.loads((p/'control-reset.json').read_text())['render']==32000
groom=json.loads((R/'assets/moca/bob.json').read_text(encoding='utf-8'));assert groom['strands']==32000
surface=ScalpSurface(R/'assets/moca/character.bin');b=(R/'assets/moca/bob.hair').read_bytes()
count,points=struct.unpack_from('<II',b,4);assert (count,points)==(32000,1024000) and len(b)==128+points*12
error=0.
for i in range(count):
    root=struct.unpack_from('<3f',b,128+i*32*12);x,y,z=[root[k]-surface.center[k] for k in range(3)]
    error=max(error,math.dist(root,surface.ray(math.atan2(math.hypot(x,z),y),math.atan2(x,-z))))
assert error<1e-6
assert hashlib.sha256((R/'assets/moca/studio.hair').read_bytes()).hexdigest()=='d0280506307d870c3ddc569f84efc2b31035701c7e00d49e33dd123fddfe3136'
results['all32000RootsOnScalpMaxErrorMetres']=error
results['counts']={'guides':1600,'render':32000,'points':points,'bones':315}
results['controllerAPI']='pause / step / reset / style switch / camera-state isolation passed; physical mouse/resize not tested'
results['assetHashes']={n:hashlib.sha256((R/'assets/moca'/n).read_bytes()).hexdigest() for n in ('character.bin','bob.hair','bob.materials','studio.hair','surface.bin')}
(O/'validation.json').write_text(json.dumps(results,indent=2));print(json.dumps(results,indent=2))
