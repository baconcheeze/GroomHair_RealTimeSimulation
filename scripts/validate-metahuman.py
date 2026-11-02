"""Bounded app-owned import/motion/reset checks, not numerical solver research."""
import hashlib,json,math,subprocess,time,sys
from pathlib import Path
R=Path(__file__).resolve().parents[1]
meta=json.loads((R/'assets/moca/metahuman.json').read_text())
O=R/('artifacts/metahuman3-match/regression' if 'referenceMatch' in meta else 'artifacts/hair-side-redesign/regression' if meta.get('newlyAuthoredStrands',0) else 'artifacts/hair-salon-finish/verified');O.mkdir(parents=True,exist_ok=True)
if '--accepted' in sys.argv:
    O=R/'artifacts/metahuman3-match/accepted-regression';O.mkdir(parents=True,exist_ok=True)
if 'sideburnDesign' in meta:
    O=R/'artifacts/sideburn-v14/regression';O.mkdir(parents=True,exist_ok=True)
assert meta['originalSourceStrands']==33512 and meta['renderStrands']+meta['removedStrands']==33512+meta.get('newlyAuthoredStrands',0)
assert meta['removedStrands']==0
assert all(g['strandCount']==g['sourceStrandCount'] and g['trimmedStrands']==0 for g in meta['groups'] if not g.get('authored'))
assert sum(g['strandCount'] for g in meta['groups'] if g.get('authored'))==meta.get('newlyAuthoredStrands',0)
regionCounts={}
for g in meta['groups']:
    if g['strandCount']:regionCounts[g['region']]=regionCounts.get(g['region'],0)+g['strandCount']
if 'materialRegionCounts' in meta:regionCounts={int(k):v for k,v in meta['materialRegionCounts'].items()}
expectedGuides=sum(math.ceil(n*meta.get('guideFractions',{}).get(str(region),.05)) for region,n in regionCounts.items())
inputHashes={n:hashlib.sha256((R/'assets/moca'/n).read_bytes()).hexdigest() for n in ['metahuman.hair','metahuman.materials','metahuman.ears','metahuman.skull','character.bin','bob.hair','studio.hair','naruto-run.bin']}
inputHashes['JoltDisplay.hlsl']=hashlib.sha256((R/'shaders/JoltDisplay.hlsl').read_bytes()).hexdigest()
runs=[]
completed=json.loads((O/'runs.json').read_text()) if '--resume' in sys.argv and (O/'runs.json').exists() else []
cases=[('Release','front',['--view','bust','--steps','180','--capture-every','30','--reset-check']),
       ('Release','side',['--view','bust-side','--steps','120','--capture-every','30','--reset-check']),
       ('Release','walk',['--walk-test','--steps','180','--capture-every','30','--reset-check']),
       ('Debug','front',['--view','bust','--steps','60','--capture-every','30','--reset-check']),
       ('Release','controls',['--view','bust','--control-check']),
       ('Release','bob',['--hairstyle','bob','--view','bust','--steps','5','--capture-every','5','--reset-check']),
       ('Release','long',['--hairstyle','long','--view','bust','--steps','5','--capture-every','5','--reset-check']),
       ('Release','ui',['--view','bust','--frames','3','--paused','--capture-ui'])]
for config,name,args in cases:
    exe=R/f'build/jolt-only/bin/{config}/HairLabDX12.exe';out=O/config/name
    previous=next((x for x in completed if x['configuration']==config and x['test']==name and x['exit']==0 and x.get('inputSha256')==inputHashes and x['exeSha256']==hashlib.sha256(exe.read_bytes()).hexdigest()),None)
    if previous:
        runs.append(previous);print(config,name,'reuse recorded PASS',flush=True);continue
    command=[str(exe),'--hairstyle','metahuman',*args,'--capture-dir',str(out),'--log',str(out/'app.log')]
    start=time.perf_counter();proc=subprocess.Popen(command,cwd=R.parent)
    try:code=proc.wait(timeout=120)
    except subprocess.TimeoutExpired:
        proc.terminate();proc.wait(timeout=10);raise RuntimeError('Own bounded MetaHuman test timed out')
    assert code==0,(config,name,code)
    statePaths=sorted(out.glob('step-*.json'))
    if name=='controls':
        # The controller intentionally overwrites automatic step-0 captures
        # while testing the old bob. Score its named MetaHuman snapshots.
        statePaths=[out/n for n in ['idle-before.json','idle-after.json','single-step.json','camera-before.json','camera-after.json','control-reset.json']]
        before=json.loads((out/'idle-before.json').read_text());after=json.loads((out/'idle-after.json').read_text())
        assert before['guideTips']==after['guideTips'] and before['renderTips']==after['renderTips']
        assert json.loads((out/'single-step.json').read_text())['step']==1
        assert json.loads((out/'style-switched.json').read_text())['hairstyle']=='bob'
        assert json.loads((out/'control-reset.json').read_text())['step']==0
    states=[json.loads(p.read_text()) for p in statePaths]
    assert states and all(s['finite'] and s['maxRootAttachmentErrorMeters']<1e-5 for s in states)
    if name not in ('bob','long'):
        for s in states:
            assert s['hairstyle']=='metahuman' and s['render']==meta['renderStrands'] and s['guides']==expectedGuides
            assert len(s['materials'])==len(regionCounts) and set(s['guideMaterials'])==set(regionCounts)
            assert s['hulls']==5 and s['hullJointIds'].count(4)==3
    resetPngEqual=None
    if '--reset-check' in args:
        # Keep strict image equality as a separate observed result. A reset
        # must reproduce the actual state, not merely look similar in a PNG.
        assert json.loads((out/'step-0000.json').read_text())==json.loads((out/'reset-0000.json').read_text())
        resetPngEqual=(out/'step-0000.png').read_bytes()==(out/'reset-0000.png').read_bytes()
    log=(out/'app.log').read_text()
    if config=='Debug':assert 'D3D12 debug audit: no errors' in log
    if name=='controls':assert 'PASS Moca controller' in log
    runs.append({'configuration':config,'test':name,'command':command,'exit':code,
        'hostWallSecondsIncludingInitializationCapturesAndReadback':time.perf_counter()-start,
        'maxRootErrorMetres':max(s['maxRootAttachmentErrorMeters'] for s in states),
        'exeSha256':hashlib.sha256(exe.read_bytes()).hexdigest(),'inputSha256':inputHashes,'resetStateExact':True if '--reset-check' in args else None,'strictResetPngBytesEqual':resetPngEqual})
    (O/'runs.json').write_text(json.dumps(runs,indent=2));print(config,name,'PASS',flush=True)
def rms(a,b):return math.sqrt(sum(math.dist(x,y)**2 for x,y in zip(a,b))/len(a))
p=O/'Release/front';a=json.loads((p/'step-0000.json').read_text());b=json.loads((p/'step-0180.json').read_text())
motion={k:rms(a[k],b[k]) for k in ['guideTips','renderTips']}
assert all(x>1e-5 for x in motion.values()) # Head-local: excludes rigid head motion.
source=R/'assets/MetaHuman'
report={'runs':runs,'headLocalTipRmsDeformationMetres':motion,'profile':meta['profile'],
    'originalSourceStrands':33512,'removedStrands':meta['removedStrands'],'retainedStrands':meta['renderStrands'],'newlyAuthoredStrands':meta.get('newlyAuthoredStrands',0),'guideCount':expectedGuides,'regionCounts':regionCounts,
    'sourceSha256':{str(p.relative_to(source)):hashlib.sha256(p.read_bytes()).hexdigest() for p in source.rglob('*') if p.is_file()},
    'stagedSha256':inputHashes,
    'qualification':'CLI/controller API, no physical mouse/keyboard/resize claim; not a performance benchmark.',
    'harnessRepair':'Initial control scoring incorrectly treated the deliberately switched bob automatic capture as MetaHuman. Named control snapshots now validate idle/pause/single-step/camera/style-switch/reset separately; no app gate relaxed.'}
report['resetImageQualification']='First v4 run had equal saved states but 6 RGB channels in two pixels differed by 1/255. Strict PNG mismatch preserved in artifacts/metahuman-silhouette/reset-byte-difference. Strict byte equality is reported separately; it is not relabeled passed. No pixel tolerance is used as a state-reset oracle.'
assert report['sourceSha256']['long_wavy_twin_ponytail_hair\\Long_Wavy_Twin_Ponytail_Hair.abc']==json.loads((R/'assets/moca/metahuman.json').read_text())['sourceSha256']
(O/'validation.json').write_text(json.dumps(report,indent=2));print('PASS moving guides AND render hair, all groups, roots, reset, debug layer, old styles and executable-relative loading')
