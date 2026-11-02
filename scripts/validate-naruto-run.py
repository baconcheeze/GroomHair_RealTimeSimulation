"""Bounded actual Jolt walking integration; no synthetic hair deformation."""
import json,math,subprocess,hashlib,re,time
from pathlib import Path
R=Path(__file__).resolve().parents[1];O=R/'artifacts/naruto-run/final';O.mkdir(parents=True,exist_ok=True)
runs=[]
cases=[
 ('Release','walk',['--walk-test','--steps','390','--capture-every','15','--reset-check']),
 ('Release','stationary',['--view','quarter','--steps','390','--capture-every','30','--reset-check']),
 ('Debug','walk',['--walk-test','--steps','180','--capture-every','30','--reset-check']),
 ('Release','controller',['--walk-check','--capture-every','30']),
 ('Debug','controller',['--walk-check','--capture-every','30']),
 ('Release','studio',['--view','bust','--steps','30','--capture-every','30','--reset-check']),
 ('Release','studio-controls',['--view','bust','--control-check']),
 ('Release','ui',['--view','quarter','--frames','3','--paused','--capture-ui']),
 ('Release','clock',['--view','quarter','--frames','90','--run','--no-animation','--capture-every','1000'])]
for config,name,args in cases:
    p=O/config/name;exe=R/f'build/jolt-only/bin/{config}/HairLabDX12.exe'
    cmd=[str(exe),*args,'--capture-dir',str(p),'--log',str(p/'app.log')]
    start=time.perf_counter();proc=subprocess.Popen(cmd,cwd=R)
    try:code=proc.wait(timeout=150)
    except subprocess.TimeoutExpired:proc.terminate();proc.wait(timeout=10);raise RuntimeError('Own walking test timed out')
    assert code==0,(config,name,code)
    runs.append({'configuration':config,'name':name,'exit':code,'hostWallSecondsIncludingStartupAndCaptures':time.perf_counter()-start,'command':cmd,'exeSha256':hashlib.sha256(exe.read_bytes()).hexdigest()})
    (O/'runs.json').write_text(json.dumps(runs,indent=2));print(config,name,code,flush=True)
def states(p):return [json.loads(f.read_text()) for f in sorted(p.glob('step-*.json'))]
def rms(a,b):return math.sqrt(sum(math.dist(x,y)**2 for x,y in zip(a,b))/len(a))
result={}
for config in ('Release','Debug'):
    p=O/config/'walk';ss=states(p);initial=ss[0]
    assert initial['guides']==1600 and initial['render']==32000
    assert json.loads((p/'reset-0000.json').read_text())==initial
    assert (p/'reset-0000.png').read_bytes()==(p/'step-0000.png').read_bytes()
    maxV=max(math.dist(s['characterVelocityWorld'],[0,0,0]) for s in ss)
    maxDerivativeError=maxHullError=0
    for s in ss:
        assert s['finite'] and s['maxRootAttachmentErrorMeters']<1e-5 and not s['extraInertialForceApplied']
        assert s['materials'][0]==s['materials'][1]
        assert math.dist(s['hairWorldColumns'][12:15],s['headPositionWorld'])<1e-6
        for matrix,joint in zip(s['hullWorldColumns'],s['hullJointIds']):
            expected=s['posedJointPositionsWorld'][joint] if joint!=4294967295 else [0,0,0]
            maxHullError=max(maxHullError,math.dist(matrix[12:15],expected))
        if s['step']:
            v=[(x-y)/s['dt'] for x,y in zip(s['headPositionWorld'],s['headPreviousPositionWorld'])]
            a=[(x-y)/s['dt'] for x,y in zip(s['headVelocityWorld'],s['headPreviousVelocityWorld'])]
            maxDerivativeError=max(maxDerivativeError,max(abs(x-y) for x,y in zip(v,s['headVelocityWorld'])),max(abs(x-y) for x,y in zip(a,s['headAccelerationWorld'])))
    assert maxV<=json.loads((R/'assets/moca/naruto-run.json').read_text())['referenceSpeedMetresPerSecond']+.0001 and maxHullError<1e-5 and maxDerivativeError<.002
    assert math.dist(ss[-1]['characterPositionWorld'],initial['characterPositionWorld'])>.5
    assert max(rms(initial['posedJointPositionsWorld'],s['posedJointPositionsWorld']) for s in ss)>.1
    result[config]={'exactReset':True,'finite':True,'maxSpeedMetresPerSecond':maxV,'maxRootAttachmentErrorMetres':max(s['maxRootAttachmentErrorMeters'] for s in ss),'maxHullJointPositionErrorMetres':maxHullError,'maxStoredDerivativeReconstructionError':maxDerivativeError,'steps':ss[-1]['step']}
    p=O/config/'controller'
    assert json.loads((p/'initial.json').read_text())==json.loads((p/'reset.json').read_text())==json.loads((p/'returned.json').read_text())
    assert math.dist(json.loads((p/'stopped.json').read_text())['characterVelocityWorld'],[0,0,0])<1e-6
    assert 'PASS walking controller' in (p/'app.log').read_text()
moving={s['step']:s for s in states(O/'Release/walk')};still={s['step']:s for s in states(O/'Release/stationary')}
curve={k:{key:rms(moving[k][key],still[k][key]) for key in ('guideTips','renderTips')} for k in sorted(set(moving)&set(still))}
assert max(c['guideTips'] for c in curve.values())>1e-5 and max(c['renderTips'] for c in curve.values())>1e-5
result['walkingVsStationaryHeadLocalTipRmsMetres']=curve
clock=(O/'Release/clock/app.log').read_text();match=re.search(r'simulated_s=([\d.]+), discarded_wall_s=([\d.]+)',clock);assert match and float(match[1])>0
result['clock']={'simulatedSeconds':float(match[1]),'discardedWallSeconds':float(match[2]),'renderedFrames':90,'qualification':'wall-clock integration smoke, not GPU timing'}
original={'character.bin':'628dd9826eef4d167315940fa4f35d7b8cebf68000ef8a827e0512d028b5ed46','bob.hair':'4d26c46923d4c19eda2b95cc053b85162c4f1bfd494e33b1189b5d3cec74b9a9'}
for n,h in original.items():assert hashlib.sha256((R/'assets/moca'/n).read_bytes()).hexdigest()==h
result['originalPhysicalAssetsUnchanged']=original
(O/'validation.json').write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2))
