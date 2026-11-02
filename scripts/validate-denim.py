"""Bounded garment integration checks; no solver tuning or source re-import."""
import hashlib,json,math,subprocess,struct
from pathlib import Path
R=Path(__file__).resolve().parents[1];O=R/'artifacts/denim/final';O.mkdir(parents=True,exist_ok=True)
runs=[]
for config,name,args in [
 ('Release','motion',['--view','bust','--steps','120','--capture-every','15','--reset-check']),
 ('Release','side',['--view','bust-side','--frames','3']),
 ('Release','back',['--view','bust-back','--frames','3']),
 ('Release','without',['--view','bust','--no-jacket','--steps','30','--capture-every','30']),
 ('Debug','motion',['--view','bust','--steps','30','--capture-every','15','--reset-check']),
 ('Release','controls',['--control-check']),
 ('Release','ui',['--view','bust','--frames','3','--capture-ui'])]:
    p=O/config/name;exe=R/f'build/jolt-only/bin/{config}/HairLabDX12.exe'
    cmd=[str(exe),*args,'--capture-dir',str(p),'--log',str(p/'app.log')]
    process=subprocess.Popen(cmd,cwd=R)
    try:code=process.wait(timeout=120)
    except subprocess.TimeoutExpired:process.terminate();process.wait(timeout=10);raise
    assert code==0,(config,name,code)
    runs.append({'configuration':config,'name':name,'exit':code,'command':cmd,'exeSha256':hashlib.sha256(exe.read_bytes()).hexdigest()})
    (O/'runs.json').write_text(json.dumps(runs,indent=2));print(config,name,code,flush=True)
result={}
for config in ('Release','Debug'):
    p=O/config/'motion';ss=[json.loads(f.read_text()) for f in sorted(p.glob('step-*.json'))];a=ss[0]
    assert a['denimJacket'] and a['jacketTriangles']>0 and a['guides']==1600 and a['render']==32000
    assert all(s['finite'] and s['maxRootAttachmentErrorMeters']<1e-5 for s in ss)
    assert json.loads((p/'reset-0000.json').read_text())==a
    assert (p/'reset-0000.png').read_bytes()==(p/'step-0000.png').read_bytes()
    result[config]={'finite':True,'exactReset':True,'maxRootAttachmentErrorMetres':max(s['maxRootAttachmentErrorMeters'] for s in ss),'steps':max(s['step'] for s in ss)}
for step in (0,30):
    a=json.loads((O/f'Release/motion/step-{step:04}.json').read_text());b=json.loads((O/f'Release/without/step-{step:04}.json').read_text())
    assert a.pop('denimJacket') and not b.pop('denimJacket');assert a==b
result['jacketVisibilityDoesNotChangePhysics']=True
p=O/'Release/controls'
for prefix in ('idle','camera'):assert (p/f'{prefix}-before.json').read_bytes()==(p/f'{prefix}-after.json').read_bytes()
result['controllerChecks']='existing pause/step/reset/style/camera API; no physical mouse/resize claims'
expected={'character.bin':'628dd9826eef4d167315940fa4f35d7b8cebf68000ef8a827e0512d028b5ed46',
          'bob.hair':'4d26c46923d4c19eda2b95cc053b85162c4f1bfd494e33b1189b5d3cec74b9a9',
          'studio.hair':'d0280506307d870c3ddc569f84efc2b31035701c7e00d49e33dd123fddfe3136'}
for name,value in expected.items():assert hashlib.sha256((R/'assets/moca'/name).read_bytes()).hexdigest()==value
outfit=json.loads((R/'assets/moca/outfit.json').read_text());assert hashlib.sha256((R/'assets/moca/surface.bin').read_bytes()).hexdigest()==outfit['surfaceSha256']
result['originalPhysicalAssetsUnchanged']=expected
data=(R/'assets/moca/denim-jacket.bin').read_bytes();magic,version,nv,ni=struct.unpack_from('<4sIII',data)
assert magic==b'DNMJ' and version==1 and len(data)==16+nv*92+ni*4
for i in range(nv):
    s=struct.unpack_from('<2fII4f4I4f',data,16+nv*28+i*64)
    assert all(j<315 for j in s[8:12]) and abs(sum(s[12:16])-1)<1e-5
result['jacket']={'vertices':nv,'triangles':ni//3,'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest()}
(O/'validation.json').write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2))
