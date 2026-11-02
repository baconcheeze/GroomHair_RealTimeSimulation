"""Decode the complete frozen input and inventory existing comparison evidence.
Does not execute a solver, rewrite historical evidence, or infer missing hashes.
"""
from pathlib import Path
import hashlib,json,struct,math

root=Path(__file__).resolve().parents[1]
out=root/'artifacts/diagnosis'/(root/'build/diagnosis-run.txt').read_text().strip()
prior=root/'artifacts/autonomous/20261029T173940Z'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
raw=(out/'frozen/fixture.hfd').read_bytes();offset=0
def unpack(fmt):
    global offset
    n=struct.calcsize(fmt);v=struct.unpack_from(fmt,raw,offset);offset+=n;return v
names=['magic','version','sourceId','guideIndex','seed','step','vertices','segments','bends','initializer','sweeps','pinX','pinQ']
fixture=dict(zip(names,unpack('<13I')))
fixture.update(zip(['h','ss','bt','gx','gy','gz'],unpack('<6f')))
fixture['localToWorldRowVector']=list(unpack('<16f'))
fixture['worldToLocalRotationRowVector']=list(unpack('<16f'))
fixture['rootPositionTarget']=list(unpack('<3f'));fixture['rootOrientationTarget']=list(unpack('<4f'))
sections={}
def array(name,count,width):
    start=offset;v=unpack('<'+'f'*(count*width))
    fixture[name]=[list(v[j:j+width]) for j in range(0,len(v),width)] if width>1 else list(v)
    sections[name]={'offset':start,'count':count,'stride':width*4,'sha256':hashlib.sha256(raw[start:offset]).hexdigest()}
for n in ['preX','preV','accelerationHistory','y','initialX','restX','historicalGPU','historicalReference']:array(n,fixture['vertices'],3)
for n in ['preQ','initialQ','restQ']:array(n,fixture['segments'],4)
array('restBend',fixture['bends'],4)
array('restLength',fixture['segments'],1);array('mass',fixture['vertices'],1);array('radius',fixture['segments'],1)
assert offset==len(raw)
f32=lambda x:struct.unpack('<f',struct.pack('<f',x))[0]
fixture['units']='meters, seconds, kg; LH already-converted neck-local; Hamilton xyzw/+Z'
fixture['dataset']='FemaleHairRest';fixture['bodyCollision']=False
fixture['byteLength']=len(raw);fixture['binarySHA256']=sha(out/'frozen/fixture.hfd');fixture['sections']=sections
fixture['effectiveFloatInvH2']=f32(1/f32(fixture['h']*fixture['h']))
fixture['kssFloat']=[f32(fixture['ss']*l) for l in fixture['restLength']]
fixture['kbtFloat']=[f32(f32(4*fixture['bt'])/l) for l in fixture['restLength'][:-1]]
fixture['positionDiagonalDouble']=[fixture['mass'][v]/fixture['h']**2+(fixture['ss']/fixture['restLength'][v-1] if v else 0)+(fixture['ss']/fixture['restLength'][v] if v<fixture['segments'] else 0) for v in range(fixture['vertices'])]
checks={'preXRestExact':fixture['preX']==fixture['restX'],'preQRestExact':fixture['preQ']==fixture['restQ'],'historyZero':all(v==[0,0,0] for v in fixture['accelerationHistory'])}
pred=[];inert=[]
for v,(x,velocity) in enumerate(zip(fixture['preX'],fixture['preV'])):
    n=[f32(x[k]+f32(fixture['h']*velocity[k])) for k in range(3)]
    inert.append(n if v else fixture['rootPositionTarget'])
    pred.append([f32(n[k]+f32(f32(fixture['h']*fixture['h'])*fixture[['gx','gy','gz'][k]])) for k in range(3)] if v else fixture['rootPositionTarget'])
checks['floatPredictorExact']=pred==fixture['y'];checks['chosenInitialInertialExact']=inert==fixture['initialX'];checks['rootVZero']=fixture['preV'][0]==[0,0,0]
fixture['checks']=checks
(out/'fixture.json').write_text(json.dumps(fixture,indent=2),encoding='utf-8')

core=['src/RestData.cpp','src/RestData.h','src/GuideState.cpp','src/GuideState.h','src/GuideGpu.cpp','src/GuideGpu.h','src/DynamicsSession.h','src/Dynamics.cpp','src/Dynamics.h','src/WindowMath.cpp','src/WindowSolver.cpp','src/WindowSolver.h','src/QuaternionMath.cpp','src/AutonomousBench.cpp','src/AutonomousReference.cpp','shaders/dynamics/Dynamics.hlsl','shaders/window/WindowSolve.hlsl','shaders/window/PositionLocal.hlsl','shaders/orientation/GuideOrientationLocal.hlsl','shaders/orientation/GuideQuaternion.hlsl']
bundles={}
for name in ['baseline',*[f'cycle-{n:02d}' for n in range(1,13)]]:
    base=prior/name if name=='baseline' else prior/name/'source'
    bundles[name]={'sourceSHA256':{n:sha(base/n) if (base/n).exists() else None for n in core},'snapshotPresent':base.exists()}
bundles['diagnosis']={'sourceSHA256':{n:sha(root/n) for n in core}}

rows=[]
def load(p):
    b=p.read_bytes();return json.loads(b.decode('utf-16' if b.startswith(b'\xff\xfe') else 'utf-8-sig'))
for p in sorted(prior.rglob('*.json')):
    relative=p.relative_to(prior)
    if any(n in relative.parts for n in ['source','runtime','best-tested-runtime','pre-final-rebuild']):continue
    try:j=load(p)
    except (UnicodeError,ValueError):continue
    if not isinstance(j,dict):continue
    kind='trajectory' if 'trajectoryRMS' in j else 'frozen' if 'sameTargetBytes' in j else 'reference' if j.get('method','').startswith('independent double') else 'timing' if 'medianMs' in j else None
    if not kind:continue
    cycle=next((n for n in relative.parts if n.startswith('cycle-') and n[6:].isdigit()),None)
    bundle=cycle or 'baseline';actual=[c['diagnostics'] for c in j.get('checkpoints',[]) if 'diagnostics' in c]
    gravity=actual[-1].get('gravitySolver') if actual else j.get('gravitySolver')
    inferred=gravity is None and bundles.get(bundle,{}).get('sourceSHA256',{}).get('src/AutonomousBench.cpp') is not None
    actual_h=j.get('h',actual[-1].get('h') if actual else None)
    binary=p.with_suffix('.bin')
    if actual_h is None and kind=='reference' and binary.exists():
        header=binary.read_bytes()[:16]
        if len(header)==16 and struct.unpack('<I',header[:4])[0]==0x31545a41:actual_h=struct.unpack('<f',header[12:16])[0]
    rows.append({'path':str(p.relative_to(root)),'resultSHA256':sha(p),'kind':kind,'bundle':bundle,'gravitySolver':gravity,'gravitySource':'recorded' if gravity is not None else 'not recorded; constructor transform is source evidence only' if inferred else 'not recorded, exact source snapshot unavailable',
                 'sourceConstructorNominalGravity':[-9.699665069580078,.06558697670698166,-1.4657117128372192] if inferred else None,
                 'transform':'inverse frame0 neck rotation (hash-matched RestData/asset)' if gravity is not None or inferred else 'unknown',
                 'h':actual_h,'hWhenUnrecorded':'benchmark default float(1/120) inferred from source; NOT an actual parameter log',
                 'sweeps':j.get('sweeps',actual[-1].get('sweepsPerSubstep') if actual else None),'checkpoints': [x.get('sweeps') for x in j.get('checkpoints',[]) if 'sweeps' in x],
                 'initializer':j.get('initializer',actual[-1].get('initializer') if actual else None),
                 'steps':j.get('steps'),'kick':j.get('kick'),'seed':j.get('seed'),
                 'materialCoefficientsSource':'SS=float(1), BT=float(1e-5) from snapshot; not stored in older report',
                 'materialSourceBoundValues':{'SS':1,'BT':f32(1e-5)} if bundles.get(bundle,{}).get('snapshotPresent') else None,
                 'pinsSource':'root x/q true in benchmark fixture; reported root error is a check, not a parameter log',
                 'dataset':'FemaleHairRest via staged manifest; subset source IDs when recorded','sourceStrandIds':j.get('sourceStrandIds'),
                 'shaderBinarySHA256':None,'binaryHashNote':'Per-cycle executable/DXIL were not generally archived; do not substitute current binary hashes'})

historical=[]
for name in ['artifacts/dynamics/Release/step-16.json','artifacts/dynamics/Release/gravity-frame/step-16.json']:
    p=root/name;j=load(p);historical.append({'path':name,'sha256':sha(p),'gravitySolver':j['gravitySolver'],'h':j['h'],'sweeps':j['sweepsPerSubstep'],'initializer':'FullPrediction from historical implementation evidence','transform':'not logged; older docs claimed identity, full matrix cannot be recovered from one vector' if 'gravityFrame' not in j else j['gravityFrame'],'sourceBinaryHashes':'not available for exact historical execution'})
before=load(out/'preserved-before.json');modified=[p for p,h in before.items() if sha(root/p)!=h];assert not modified,modified
archived=load(prior/'runtime-hashes.json')
assert all(sha(prior/'best-tested-runtime'/p)==h for p,h in archived.items())
binHashes={}
for base in [root/'build/diagnosis/bin/Release',prior/'baseline/runtime',prior/'best-tested-runtime']:
    binHashes[str(base.relative_to(root))]={str(p.relative_to(base)):sha(p) for p in (base/'shaders').rglob('*.dxil')}
comparison={'dataset':load(root/'assets/manifest.json'),'settings':{k:fixture[k] for k in ['h','ss','bt','pinX','pinQ','initializer','sweeps','sourceId','guideIndex','seed','step']},'gravityWorld':[0,-9.81,0],'gravitySolver':[fixture[k] for k in ['gx','gy','gz']],'transform':fixture['worldToLocalRotationRowVector'],'bundles':bundles,'historicalGravityGroups':historical,'existingComparisonInventory':rows,'actualArchivedShaderBinaries':binHashes,'productionPreservation':{'allBeforeHashesUnchanged':True,'filesChecked':len(before),'archivedResearchRuntimeUnchanged':True},'metricDefinitions':{'trajectoryRMS':'sqrt(sum squared position distances over all steps and vertices / count), roots included','firstFailureMarker':'per-strand RMS at one substep >= .1mm or max >=1mm; not aggregate trajectory pass/fail','matchedPositionRMS':'same frozen initial x/q and same immutable y, sqrt(mean vertex squared position distance)','conditionalGap':'F(x,qFixed)-F(xStar(qFixed),qFixed); includes constant BT, no changing targets','coupledGap':'F(actualGpu32)-F(qualified local coupled reference); not a global-optimum gap','residuals':'max free position gradient/localDiagonal; normalized q tangent stationarity and spectral/minimum branch; pinned DOFs excluded','axialStrain':'abs(length(current edge)/restLength-1); last-step maximum is different from max over all substeps','shearStretchError':'norm(edge/L0-Rotate(q,e3)); NOT equal to axial strain'}}
comparison['diagnosticCompilerManifest']={n:(root/'build/diagnosis/bin/Release/shaders'/n/'compiler.txt').read_text() for n in ['orientation','window','dynamics']}
comparison['matchedShaderVsResearchCandidate']={n:sha(root/'build/diagnosis/bin/Release/shaders'/n)==sha(prior/'best-tested-runtime/shaders'/n) for n in ['window/WindowSolve.dxil','dynamics/BeginSubstep.dxil','dynamics/EndSubstep.dxil']}
(out/'provenance.json').write_text(json.dumps(comparison,indent=2),encoding='utf-8')
(out/'production-preserved.json').write_text(json.dumps(comparison['productionPreservation'],indent=2))
result=load(out/'matched/results.json')
reference=load(out/'matched/coupled-reference-0.json')
def diff(x,y):
    e=[sum((a-b)**2 for a,b in zip(p,q)) for p,q in zip(x,y)]
    return {'RMSmeters':math.sqrt(sum(e)/len(e)),'maxMeters':math.sqrt(max(e))}
extra={'oracleFinalQvsCoupled':diff(load(out/'matched/oracle-1.json')['x'],reference['x']),
       'historicalReferenceVsMatchedReference':diff(fixture['historicalReference'],reference['x']),
       'conditionalPositionShareOfObjectiveGap':result['B'][1]['startConditionalGapJ']/result['C'][0]['gpuObjectiveGapJ'],
       'remainingGapAfterExactPositionJ':result['C'][0]['gpuObjectiveGapJ']-result['B'][1]['startConditionalGapJ'],
       'historyAndPredictorChecks':fixture['checks'],'motion':{}}
for name in ['initial','gpu32','coupled-reference-0']:
    state=load(out/f'matched/{name}.json');e=[sum((a-b)**2 for a,b in zip(p,q)) for p,q in zip(state['x'],fixture['preX'])]
    extra['motion'][name]={'RMSmeters':math.sqrt(sum(e)/len(e)),'tipMeters':math.sqrt(e[-1]),'maxSpeedReconstructed':math.sqrt(max(e))/fixture['h']}
assert (out/'frozen/fixture.hfd').read_bytes()==(out/'matched/fixture.hfd').read_bytes()
extra['binaryFixtureReplayExact']=True
(out/'interpretation-data.json').write_text(json.dumps(extra,indent=2))
manifest={str(p.relative_to(root)):sha(p) for p in (root/'diagnostics').rglob('*') if p.is_file()}
for p in (root/'build/diagnosis/bin/Release/shaders').rglob('*'):
    if p.is_file():manifest[str(p.relative_to(root))]=sha(p)
manifest['build/diagnosis/bin/Release/FirstFailure.exe']=sha(root/'build/diagnosis/bin/Release/FirstFailure.exe')
(out/'diagnostic-source-binary-hashes.json').write_text(json.dumps(manifest,indent=2))
print('Complete fixture decoded;',len(rows),'existing comparisons inventoried; production files unchanged')
