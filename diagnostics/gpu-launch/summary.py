"""Offline evaluation only; no solver corrections or references are generated here."""
from pathlib import Path
import json,hashlib,math,re,struct,sys,csv,statistics
ROOT=Path(__file__).resolve().parents[2];R=Path(sys.argv[1]).resolve()
def load(p):return json.loads(p.read_text(encoding='utf-8-sig'))
def save(p,v):p.write_text(json.dumps(v,indent=2,allow_nan=False),encoding='utf-8')
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def vector_errors(a,b):
 vals=[math.dist(x,y) for x,y in zip(a,b)];return (sum(v*v for v in vals)/len(vals))**.5,max(vals)
def qerror(a,b):return max(min(math.dist(x,y),math.dist(x,[-t for t in y])) for x,y in zip(a,b))
# Preserve every historical record. Compare current production to this run's baseline,
# and independently verify the previously published manifests.
preservation=[]
for base,manifest in [(ROOT,R/'preserved-before.json'),(ROOT/'artifacts/gpu-structured/20261031T035307Z',ROOT/'artifacts/gpu-structured/20261031T035307Z/hashes.json'),(ROOT/'artifacts/native-ls/20261031T022012Z',ROOT/'artifacts/native-ls/20261031T022012Z/hashes.json'),(ROOT,ROOT/'artifacts/strand-block/20261030T141357Z/preserved-before.json'),(ROOT/'artifacts/autonomous/20261029T173940Z/best-tested-runtime',ROOT/'artifacts/autonomous/20261029T173940Z/runtime-hashes.json')]:
 h=load(manifest);bad=[name for name,sha in h.items() if not (base/name).is_file() or digest(base/name)!=sha];preservation.append({'manifest':str(manifest.relative_to(ROOT)),'checked':len(h),'mismatches':bad})
assert not any(p['mismatches'] for p in preservation)
save(R/'preservation.json',preservation)
old=(ROOT/'diagnostics/gpu-structured/shaders/Structured.hlsl').read_text(encoding='utf-8-sig');new=(ROOT/'diagnostics/gpu-launch/shaders/Packed.hlsl').read_text()
new=new.replace('#ifndef GROUP_SIZE\n#define GROUP_SIZE 1\n#endif\n','').replace('TileSystemCount;','Padding;').replace('numthreads(GROUP_SIZE,1,1)','numthreads(1,1,1)').replace('if(tid.x>=TileSystemCount)return;','')
new=re.sub(r'#ifdef OWNERSHIP_CHECK(.*?)#endif',lambda m:m[1].split('#else',1)[1] if '#else' in m[1] else '',new,flags=re.S)
assert re.sub(r'\s','',new)==re.sub(r'\s','',old)
assert (ROOT/'diagnostics/gpu-launch/shaders/DoubleMath.hlsli').read_bytes()==(ROOT/'diagnostics/gpu-structured/shaders/DoubleMath.hlsli').read_bytes()
shader={'mathTokensEqualAfterRemovingOnlyLaunchAndIntegerInstrumentation':True,'DSqrtBytesEqual':True,'releaseLegacyDXILMatchesOriginal':{},'audit':[]}
for e in ['Form','Factor','Initial','Correct']:
 shader['releaseLegacyDXILMatchesOriginal'][e]=digest(ROOT/f'build/gpu-structured/bin/Release/shaders/{e}.dxil')==digest(R/f'runtime/Release/shaders/{e}_legacy.dxil')
assert all(shader['releaseLegacyDXILMatchesOriginal'].values())
for cfg in ['Release','Debug']:
 for p in (R/f'runtime/{cfg}/shaders').glob('*.ll'):
  s=p.read_text();bad={'fma':len(re.findall(r'\bcall[^\n]*dx\.op\.tertiary[^\n]*(?:Fma|FMad)',s)),'doubleToFloat':len(re.findall(r'fptrunc double[^\n]*to float',s)),'floatSqrt':len(re.findall(r'call float[^\n]*Sqrt',s))}
  assert not any(bad.values()),(p,bad)
  shader['audit'].append({'file':str(p.relative_to(R)),**bad})
save(R/'numerical-preservation.json',shader)
linear=[];curves=[]
for cfg in ['Release','Debug']:
 for mode,oldmode in [('replay','g64'),('formation','formation')]:
  d=R/cfg/mode;status=load(d/'STATUS.json');assert status['cases']==83
  for budget in status['budgets']:
   if budget['corrections']:assert budget['pass']==83 and budget['factorOrSolveFailures']==0
  for k in [0,1,2,3,5]:
   records=[];same=0
   for p in d.glob('*.json'):
    if p.name in ['STATUS.json','device.json','invocation.json']:continue
    rec=next(x for x in load(p)['curve'] if x['corrections']==k);records.append(rec)
    orig=load(ROOT/f'artifacts/gpu-structured/20261031T035307Z/audited/final-{cfg}/{oldmode}'/p.name)
    base=next(x for x in orig['curve'] if x['corrections']==k)
    same+=all(struct.pack('d',x)==struct.pack('d',y) for x,y in zip(rec['u'],base['u']))
   assert len(records)==83
   row={'configuration':cfg,'mode':mode,'corrections':k,'passed':sum(x['pass'] for x in records),'cases':83,'bitwiseOldPilotSolutions':same}
   for key in ['positionError','rawAngleError','scaledRelative','LSstationarity']:
    row['max_'+key]=max(x[key] for x in records)
   linear.append(row)
save(R/'linear-comparison.json',linear)
if (R/'B1-original-layout').exists():
 b1=load(R/'B1-original-layout/replay/STATUS.json');assert b1['cases']==83 and all(x['pass']==83 for x in b1['budgets'] if x['corrections'])
 assert load(R/'B1-original-layout/hybrid1/STATUS.json')=={'cases':12,'passed':12}

nonlinear=[];trajectory=[]
for cfg in ['Release','Debug']:
 assert load(R/cfg/'hybrid1/STATUS.json')=={'cases':12,'passed':12}
 for d in sorted((R/cfg/'hybrid1').iterdir()):
  if not d.is_dir():continue
  a=load(d/'final.json');b=load(ROOT/'artifacts/native-ls/20261031T022012Z/runtime/isolated-nonlinear'/d.name/'final.json');res=load(d/'result.json');native=load(ROOT/'artifacts/native-ls/20261031T022012Z/runtime/isolated-nonlinear'/d.name/'result.json')
  rms,mx=vector_errors(a['x'],b['x']);qq=qerror(a['q'],b['q']);assert res['qualified'] and mx<1e-9 and qq<1e-7
  nonlinear.append({'configuration':cfg,'fixture':d.name,'positionRMS':rms,'positionMax':mx,'quaternionChordMax':qq,'objectiveDifference':res['F']-native['F'],**res})
 st=load(R/cfg/'dynamics16-c1/STATUS.json');assert st['pass'] and st['correctionsPerLinearSolve']==1
 for name in ['primary','heldout-slot0','heldout-slot2']:
  a=load(R/cfg/'dynamics16-c1'/f'{name}.json');b=load(ROOT/'artifacts/native-ls/20261031T022012Z/runtime/isolated-dynamics'/f'{name}-16.json');assert a['pass'] and a['completed']==16
  allx=[];allb=[];vm=qm=0
  for x,y in zip(a['points'],b['points']):
   allx.extend(x['x']);allb.extend(y['x']);vm=max(vm,vector_errors(x['v'],y['v'])[1]);qm=max(qm,qerror(x['q'],y['q']))
  rms,mx=vector_errors(allx,allb);assert rms<1e-4 and mx<1e-3
  tip=[x-y for x,y in zip(a['points'][-1]['x'][-1],a['points'][0]['x'][-1])]
  trajectory.append({'configuration':cfg,'fixture':name,'correctionsPerLinearSolve':1,'positionRMS':rms,'positionMax':mx,'velocityMaxDifference':vm,'quaternionChordMax':qm,'maxStrain':max(x['strain'] for x in a['points']),'maxPinError':max(x['pinError'] for x in a['points']),'tipDisplacement':tip,'last':{k:v for k,v in a['points'][-1].items() if k not in ['x','q','v','y','acceleration']}})
save(R/'hybrid-comparison.json',nonlinear);save(R/'trajectory-comparison.json',trajectory)
timing=load(R/'study/timing.json');summaries=[]
def pct(a,q):return sorted(a)[min(len(a)-1,math.ceil(q*len(a))-1)]
for r in timing['runs']:
 s={k:v for k,v in r.items() if k!='raw'}
 s['stageStats']={field:{'median':statistics.median([x[field] for x in r['raw']]),'P95':pct([x[field] for x in r['raw']],.95),'min':min(x[field] for x in r['raw']),'max':max(x[field] for x in r['raw'])} for field in r['raw'][0]}
 n=r['systems'];tile=r['tile'];g=r['group'];s['groupsPerStage']=sum(math.ceil(min(tile,n-base)/g) for base in range(0,n,tile));s['totalGroups']=s['groupsPerStage']*(2+r['corrections']);summaries.append(s)
save(R/'timing-summary.json',summaries)
with (R/'timing-summary.csv').open('w',newline='') as f:
 w=csv.writer(f);w.writerow(['phase','name','systems','group','tile','corrections','dispatches','factorMedian','initialMedian','correctionMedian','totalMedian','totalP95','hostWallMedian'])
 for r in summaries:w.writerow([r[k] for k in ['phase','name','systems','group','tile','corrections','dispatches']]+[r['stageStats'][k]['median'] for k in ['factor','initial','correction','total']]+[r['stageStats']['total']['P95'],r['stageStats']['hostWaitReadbackWall']['median']])
status={'classification':'launch/batching materially improves throughput','selection':load(R/'study/SELECTION.json'),'linear':{'Debug':'83/83 c1,c5 stored and row-formed','Release':'83/83 c1,c5 stored and row-formed'},'hybrid':'12/12 c1 Debug and Release','dynamics':'3 x 16 steps c1 Debug and Release','productionUnchanged':True,'endToEndGPU':False}
save(R/'STATUS.json',status)
print(json.dumps(status,indent=2));print('Max trajectory error',max(x['positionMax'] for x in trajectory))
