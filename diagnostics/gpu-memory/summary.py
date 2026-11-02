"""Offline evaluation of immutable native outputs; no solve or correction generation."""
from pathlib import Path
import json,hashlib,math,statistics,csv,random,struct,sys,re
ROOT=Path(__file__).resolve().parents[2];R=Path(sys.argv[1]).resolve()
def load(p):return json.loads(p.read_text(encoding='utf-8-sig'))
def save(name,data):(R/name).write_text(json.dumps(data,indent=2,allow_nan=False),encoding='utf-8')
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def pct(v,q):return sorted(v)[min(len(v)-1,math.ceil(q*len(v))-1)]
def dist(a,b):
 v=[math.dist(x,y) for x,y in zip(a,b)];return math.sqrt(sum(t*t for t in v)/len(v)),max(v)
def qdist(a,b):return max(min(math.dist(x,y),math.dist(x,[-z for z in y])) for x,y in zip(a,b))
# Preserve baseline source, production, assets, and every published historical record.
preserved=[]
for base,manifest in [(ROOT,R/'preserved-before.json'),(ROOT/'artifacts/gpu-launch/20261031T053500Z',ROOT/'artifacts/gpu-launch/20261031T053500Z/hashes.json'),(ROOT/'artifacts/gpu-structured/20261031T035307Z',ROOT/'artifacts/gpu-structured/20261031T035307Z/hashes.json'),(ROOT/'artifacts/native-ls/20261031T022012Z',ROOT/'artifacts/native-ls/20261031T022012Z/hashes.json'),(ROOT,ROOT/'artifacts/strand-block/20261030T141357Z/preserved-before.json'),(ROOT/'artifacts/autonomous/20261029T173940Z/best-tested-runtime',ROOT/'artifacts/autonomous/20261029T173940Z/runtime-hashes.json')]:
 h=load(manifest);bad=[name for name,v in h.items() if not (base/name).is_file() or sha(base/name)!=v];assert not bad,bad;preserved.append({'manifest':str(manifest.relative_to(ROOT)),'files':len(h),'mismatches':bad})
save('preservation.json',preserved)
a=(ROOT/'diagnostics/gpu-launch/shaders/Packed.hlsl').read_text();b=(ROOT/'diagnostics/gpu-memory/shaders/Packed.hlsl').read_text();assert a[a.index('uint2 Block('):]==b[b.index('uint2 Block('):];assert (ROOT/'diagnostics/gpu-launch/shaders/DoubleMath.hlsli').read_bytes()==(ROOT/'diagnostics/gpu-memory/shaders/DoubleMath.hlsli').read_bytes()
shader={'bodyAfterAddressHelpersExact':True,'DSqrtBytesExact':True,'baselineDXIL':load(R/'baseline-dxil-equality.json'),'audit':[]}
assert all(shader['baselineDXIL'].values())
for cfg in ['Release','Debug']:
 for p in (R/'runtime'/cfg/'shaders').glob('*.ll'):
  text=p.read_text();bad={'doubleToFloat':len(re.findall(r'fptrunc double[^\n]*to float',text)),'floatSqrt':len(re.findall(r'call float[^\n]*Sqrt',text)),'FMA':len(re.findall(r'call[^\n]*dx\.op\.tertiary[^\n]*(?:Fma|FMad)',text))};assert not any(bad.values()),(p,bad);shader['audit'].append({'file':str(p.relative_to(R)),**bad})
save('numerical-preservation.json',shader)
candidateIndex=load(R/'study/SELECTION.json')['accuracyCandidate'];chosen=['baseline','work32','workinput32'][candidateIndex]
linear=[]
for cfg,mode,layout in [('Release','replay','work32'),('Release','replay','workinput32'),('Release','formation','work32'),('Release','formation','workinput32'),('Debug','replay',chosen),('Debug','formation',chosen)]:
 folder=R/cfg/layout/mode;st=load(folder/'STATUS.json');assert st['cases']==83
 for k in [0,1,2,3,5]:
  records=[];same=0
  for p in folder.glob('*.json'):
   if p.name in ['STATUS.json','device.json','invocation.json']:continue
   x=next(c for c in load(p)['curve'] if c['corrections']==k);records.append(x)
   old=load(ROOT/'artifacts/gpu-launch/20261031T053500Z'/cfg/mode/p.name);old=next(c for c in old['curve'] if c['corrections']==k)
   same+=len(x['u'])==len(old['u']) and all(struct.pack('<d',v)==struct.pack('<d',w) for v,w in zip(x['u'],old['u']))
  assert len(records)==83
  if k:assert all(x['pass'] for x in records)
  linear.append({'config':cfg,'mode':mode,'layout':layout,'corrections':k,'pass':sum(x['pass'] for x in records),'bitwiseBaselineCases':same,**{'max_'+name:max(x[name] for x in records) for name in ['positionError','rawAngleError','scaledRelative','LSstationarity']}})
save('linear-summary.json',linear)
assert load(R/'codec/codec.json')['passed']==28
assert load(R/'codec-audit/metadata-alias-audit.json')['passed']==28
if (R/'batch-bit-audit/batch-bit-audit.json').exists():assert load(R/'batch-bit-audit/batch-bit-audit.json')['nonBitwiseSlots']==0
owners=[]
for folder in [R/'ownership/work32',R/'ownership/workinput32',R/'ownership-Debug'/chosen]:
 x=load(folder/'ownership.json');assert x['runsPassed']==28 and x['validSlots']==9914;assert all(t['pass'] and t['maxControlScaledDifference']==0 for t in x['runs']);owners.append({'directory':str(folder.relative_to(R)),**x})
save('ownership-summary.json',owners)
nonlinear=[];trajectories=[]
for cfg in ['Release','Debug']:
 folder=R/cfg/chosen;assert load(folder/'hybrid1/STATUS.json')=={'cases':12,'passed':12}
 for d in sorted((folder/'hybrid1').iterdir()):
  if not d.is_dir():continue
  a=load(d/'final.json');b=load(ROOT/'artifacts/native-ls/20261031T022012Z/runtime/isolated-nonlinear'/d.name/'final.json');rr=load(d/'result.json');ref=load(ROOT/'artifacts/native-ls/20261031T022012Z/runtime/isolated-nonlinear'/d.name/'result.json');rms,mx=dist(a['x'],b['x']);qq=qdist(a['q'],b['q']);assert rr['qualified'] and mx<1e-9 and qq<1e-7;nonlinear.append({'config':cfg,'fixture':d.name,'RMS':rms,'max':mx,'quaternionChord':qq,'objectiveDifference':rr['F']-ref['F'],**rr})
 st=load(folder/'dynamics16-c1/STATUS.json');assert st['pass'] and st['correctionsPerLinearSolve']==1
 for name in ['primary','heldout-slot0','heldout-slot2']:
  a=load(folder/'dynamics16-c1'/f'{name}.json');b=load(ROOT/'artifacts/native-ls/20261031T022012Z/runtime/isolated-dynamics'/f'{name}-16.json');assert a['completed']==16 and a['pass'];aa=[];bb=[];vm=qm=0
  for x,y in zip(a['points'],b['points']):aa+=x['x'];bb+=y['x'];vm=max(vm,dist(x['v'],y['v'])[1]);qm=max(qm,qdist(x['q'],y['q']))
  rms,mx=dist(aa,bb);assert rms<1e-4 and mx<1e-3
  trajectories.append({'config':cfg,'fixture':name,'RMS':rms,'max':mx,'velocityMax':vm,'quaternionChordMax':qm,'correctionsPerLinearSolve':1,'pinMax':max(x['pinError'] for x in a['points']),'maxStrain':max(x['strain'] for x in a['points']),'tipDisplacement':[x-y for x,y in zip(a['points'][-1]['x'][-1],a['points'][0]['x'][-1])]})
save('hybrid-comparison.json',nonlinear);save('trajectory-comparison.json',trajectories)
timing=load(R/'study/timing.json');stats=[]
for row in timing['runs']:
 stats.append({**{k:v for k,v in row.items() if k!='raw'},'stage':{k:{'median':statistics.median(x[k] for x in row['raw']),'P95':pct([x[k] for x in row['raw']],.95),'min':min(x[k] for x in row['raw']),'max':max(x[k] for x in row['raw'])} for k in row['raw'][0]}})
save('timing-summary.json',stats)
memory=[]
for x in stats:
 if x['phase']!='resident-c1':continue
 n=x['systems'];base=next(y['memory'] for y in stats if y['phase']=='resident-c1' and y['systems']==n and y['layout']==0);work=next(y['memory'] for y in stats if y['phase']=='resident-c1' and y['systems']==n and y['layout']==1)
 m=dict(x['memory']);m.update({'systems':n,'layout':x['layout'],'canonicalInputLogical':base['inputUsed'],'canonicalMetadataLogical':n*64,'canonicalNumericInputLogical':work['numericInputLogical'],'canonicalScratchLogical':work['canonicalScratch'],'canonicalNumericWorkLogical':work['numericWorkLogical'],'canonicalStatusLogical':n*32})
 m['persistentCommittedBuffers']=sum(m[k] for k in ['inputCommitted','scratchCommitted','uploadCommitted','readbackCommitted','timestampCommitted']);m['scratchPaddingAndGuards']=m['scratchUsed']-work['canonicalScratch']
 if x['layout']==2:m['legacyNumericInputReservedHoles']=work['numericInputLogical'];m['packedInputPaddingMetadataGuards']=m['inputUsed']-base['inputUsed']-work['numericInputLogical']
 memory.append(m)
save('memory-summary.json',memory)

with (R/'timing-summary.csv').open('w',newline='') as f:
 w=csv.writer(f);w.writerow(['phase','systems','layout','corrections','factorMedian','initialMedian','correctionMedian','totalMedian','totalP95','hostMedian','unpackMedian','packingMedian','uploadMedian','integratedMedian'])
 for r in stats:w.writerow([r[k] for k in ['phase','systems','layout','corrections']]+[r['stage'][k]['median'] for k in ['factor','initial','correction','total']]+[r['stage']['total']['P95']]+[r['stage'][k]['median'] for k in ['hostReadbackWait','unpack','packing','upload','integratedWall']])
paired=[];rng=random.Random(20261031)
for phase in ['resident-c1','reverse-resident-c1']:
 a=next(x for x in timing['runs'] if x['phase']==phase and x['systems']==2839 and x['layout']==0)['raw'];b=next(x for x in timing['runs'] if x['phase']==phase and x['systems']==2839 and x['layout']==candidateIndex)['raw'];delta=[x['total']-y['total'] for x,y in zip(a,b)];boot=[statistics.median(rng.choices(delta,k=len(delta))) for _ in range(10000)];paired.append({'phase':phase,'baselineMinusBms':delta,'positive':sum(v>0 for v in delta),'sampleCount':len(delta),'medianPairedDifference':statistics.median(delta),'descriptiveBootstrapMedian95':[pct(boot,.025),pct(boot,.975)],'qualification':'paired resampling assumes sample exchangeability; not hardware-counter attribution or a guaranteed speedup'})
save('paired-variation.json',paired)
status={'classification':'resident improvement but conversion currently offsets it','retainedBaseline':'G32/tile2839/correction1 system-major','separateCandidate':['baseline','WorkInterleaved32','WorkAndInputInterleaved32'][candidateIndex],'productionUnchanged':True,'linear':f'B and C Release83/83 c1,c5 stored and formed; {chosen} Debug83/83 c1,c5 stored and formed','hybrid':f'{chosen} 12/12 Debug/Release, three strands x16 with correction1','hardwareCounters':'unmeasured; Nsight Graphics not installed','scope':'CPU nonlinear / GPU linear only'};save('STATUS.json',status);print(json.dumps(status,indent=2));print('paired',paired)
