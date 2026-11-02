"""Focused closed-scalp topology / GPU head skinning / app checks."""
import hashlib,json,math,struct,subprocess
from pathlib import Path
R=Path(__file__).resolve().parents[1];O=R/'artifacts/scalp/final';O.mkdir(parents=True,exist_ok=True)
runs=[]
for config,name,args in [
 ('Release','back',['--view','back','--steps','30','--capture-every','15','--reset-check']),
 ('Debug','back',['--view','back','--steps','30','--capture-every','15','--reset-check']),
 ('Release','front',['--view','portrait','--frames','2']),
 ('Release','side',['--view','side','--frames','2']),
 ('Release','controls',['--control-check'])]:
    out=O/config/name;exe=R/f'build/jolt-only/bin/{config}/HairLabDX12.exe'
    cmd=[str(exe),*args,'--no-hair','--capture-dir',str(out),'--log',str(out/'app.log')]
    p=subprocess.Popen(cmd,cwd=R)
    try:code=p.wait(timeout=60)
    except subprocess.TimeoutExpired:
        p.terminate();p.wait(timeout=10);raise RuntimeError('Own bounded scalp check timed out')
    runs.append({'command':cmd,'exit':code,'exeSha256':hashlib.sha256(exe.read_bytes()).hexdigest()})
    (O/'runs.json').write_text(json.dumps(runs,indent=2));print(config,name,code,flush=True);assert code==0
b=(R/'assets/moca/character.bin').read_bytes();head,nv=struct.unpack_from('<II',b);nt=struct.unpack_from('<I',b,8+nv*12)[0]
v=[struct.unpack_from('<3f',b,8+i*12) for i in range(nv)];off=12+nv*12+nt*12
nj=struct.unpack_from('<I',b,off)[0];ibm=struct.unpack_from('<16f',b,off+4+head*64)
def transform(m,p):return [sum(m[j*4+i]*p[j] for j in range(3))+m[12+i] for i in range(3)]
topology=json.loads((R/'assets/moca/scalp-patch.json').read_text());assert topology['allPatchEdgesHaveTwoIncidentFaces']
base=nv-topology['addedVertices'];results={'topology':topology,'configurations':{}}
for config in ('Release','Debug'):
    out=O/config/'back';states=[json.loads(p.read_text()) for p in sorted(out.glob('step-*.json'))]
    assert len(states)==3
    skinerror=max(math.dist(transform(ibm,v[i]),s['scalpVertices'][i]) for s in states for i in range(base,nv))
    assert skinerror<1e-6,skinerror
    def world(s):return [transform(s['hairWorldColumns'],s['scalpVertices'][i]) for i in range(base,nv)]
    before=world(states[0]);after=world(states[-1]);motion=max(math.dist(a,c) for a,c in zip(before,after));assert motion>1e-4
    assert all(s['finite'] for s in states)
    assert json.loads((out/'reset-0000.json').read_text())==states[0]
    assert (out/'reset-0000.png').read_bytes()==(out/'step-0000.png').read_bytes()
    results['configurations'][config]={'gpuPatchHeadLocalSkinErrorMeters':skinerror,'patchWorldMotionMaxMeters':motion,'finite':True,'exactReset':True}
for prefix in ('idle','camera'):
    out=O/'Release/controls';assert (out/(prefix+'-before.json')).read_bytes()==(out/(prefix+'-after.json')).read_bytes()
for name,digest in topology['hairHashes'].items():
    if name=='bob.hair' and 'scalpSha256' in json.loads((R/'assets/moca/bob.json').read_text(encoding='utf-8')):
        # A later, explicitly authored refit is allowed; it must match this scalp.
        groom=json.loads((R/'assets/moca/bob.json').read_text(encoding='utf-8'))
        assert groom['scalpSha256']==hashlib.sha256(b).hexdigest()
    else:assert hashlib.sha256((R/'assets/moca'/name).read_bytes()).hexdigest()==digest
results['longHairUnchangedAndBobScalpProvenanceValid']=True
results['manualInteraction']='Not exercised: actual mouse/resize remains manual'
(O/'validation.json').write_text(json.dumps(results,indent=2));print(json.dumps(results['configurations'],indent=2))
