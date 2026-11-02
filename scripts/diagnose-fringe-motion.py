"""Bounded same-animation material A/B. Explicit readbacks are capture-only."""
import hashlib,json,math,struct,subprocess,sys
from pathlib import Path
R=Path(__file__).resolve().parents[1]
study='material-candidate' if '--candidate' in sys.argv else ('material-balance' if '--balance' in sys.argv else 'material-ab')
O=R/'artifacts/fringe-diagnosis'/study;O.mkdir(parents=True,exist_ok=True)
exe=R/'build/jolt-only/bin/Release/HairLabDX12.exe'
profiles={
 'baseline':(.175,.06,1.,.25,True),
 'pose-off':(0.,0.,1.,.25,True),
 'weak-root':(.01,0.,.3,.25,True),
 'skin-zero':(.175,.06,1.,0.,True),
 'baseline-static':(.175,.06,1.,.25,False),
 'weak-root-static':(.01,0.,.3,.25,False),
}
if '--balance' in sys.argv:
    profiles={'medium':(.03,.008,1.,.25,True),'medium-static':(.03,.008,1.,.25,False),
              'soft':(.015,.003,1.,.25,True),'soft-static':(.015,.003,1.,.25,False)}
if '--candidate' in sys.argv:
    profiles={'candidate':(.01,.001,1.,.25,True),'candidate-static':(.01,.001,1.,.25,False)}
runs=[]
for name,(root,tip,end,skin,animate) in profiles.items():
    out=O/name
    cmd=[str(exe),'--steps','120','--capture-every','15','--fringe-pose-root',str(root),'--fringe-pose-tip',str(tip),'--fringe-pose-end',str(end),'--fringe-skin-tip',str(skin),'--capture-dir',str(out),'--log',str(out/'app.log')]
    if not animate:cmd+=['--no-animation']
    p=subprocess.Popen(cmd,cwd=R)
    try:code=p.wait(timeout=60)
    except subprocess.TimeoutExpired:p.terminate();p.wait(timeout=10);raise RuntimeError('Own bounded fringe test timed out')
    runs.append({'profile':name,'command':cmd,'exit':code,'exeSha256':hashlib.sha256(exe.read_bytes()).hexdigest()})
    (O/'runs.json').write_text(json.dumps(runs,indent=2));print(name,code,flush=True);assert code==0
allstates={name:{json.loads(p.read_text())['step']:json.loads(p.read_text()) for p in (O/name).glob('step-*.json')} for name in profiles}
def metric(a,b,ids,key):
    d=[math.dist(a[key][i],b[key][i]) for i in ids]
    return {'rmsMillimetres':1000*math.sqrt(sum(x*x for x in d)/len(d)),'maxMillimetres':1000*max(d)}
source=(R/'assets/moca/groom.materials').read_bytes();render_mats=struct.unpack_from('<'+str(struct.unpack_from('<I',source)[0])+'I',source,4)
result={}
for name,ss in allstates.items():
    a=ss[0];ids=[i for i,m in enumerate(a['guideMaterials']) if m==1];back=[i for i,m in enumerate(a['guideMaterials']) if m==0]
    assert len(ids)==320 and all(a['guideFreeVertices'][i]==31 for i in ids)
    assert all(s['finite'] and s['maxRootAttachmentErrorMeters']<1e-5 for s in ss.values())
    curve={k:metric(a,s,ids,'guideTips') for k,s in sorted(ss.items())}
    result[name]={'actualMaterial':a['materials'][1],'fringeGuides':len(ids),'freeVerticesPerGuide':31,
                  'restToCurrentTipCurve':curve,'peakGuideRmsMillimetres':max(v['rmsMillimetres'] for v in curve.values()),
                  'peakBackGuideRmsMillimetres':max(metric(a,s,back,'guideTips')['rmsMillimetres'] for s in ss.values())}
    if name+'-static' in allstates:
        static=allstates[name+'-static'];rids=[i for i,m in enumerate(render_mats) if m==1]
        result[name]['animationInducedGuideDifference']={k:metric(static[k],s,ids,'guideTips') for k,s in sorted(ss.items())}
        result[name]['animationInducedRenderDifference']={k:metric(static[k],s,rids,'renderTips') for k,s in sorted(ss.items())}
if 'baseline' in allstates:
    base=allstates['baseline'];zero=allstates['skin-zero'];fringe=[i for i,m in enumerate(base[0]['guideMaterials']) if m==1]
    result['skinZeroVsBaselinePeakMaxMillimetres']=max(metric(base[k],zero[k],fringe,'guideTips')['maxMillimetres'] for k in base)
result['poseReferencePerSecondFractions']={str(k):1-(1-k)**360 for k in (.06,.175,.01)}
result['inputHashes']={n:hashlib.sha256((R/'assets/moca'/n).read_bytes()).hexdigest() for n in ('character.bin','bob.hair','groom.materials')}
(O/'analysis.json').write_text(json.dumps(result,indent=2));print(json.dumps({name:{k:v for k,v in val.items() if not isinstance(v,dict)} if isinstance(val,dict) else val for name,val in result.items()},indent=2))
