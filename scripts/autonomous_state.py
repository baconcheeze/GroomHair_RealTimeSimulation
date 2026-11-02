"""Bounded experiment ledger. No self-relaunch; command steps are driven explicitly."""
import pathlib,json,datetime,hashlib,shutil,argparse
ROOT=pathlib.Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('action',choices=['init','start','finish','status']);p.add_argument('--hypothesis',default='');p.add_argument('--result',default='');p.add_argument('--decision',default='');a=p.parse_args()
marker=ROOT/'build/autonomous-run.txt'
if a.action=='init':
    runid=datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ');out=ROOT/'artifacts/autonomous'/runid;out.mkdir(parents=True)
    marker.parent.mkdir(exist_ok=True);marker.write_text(runid)
    baseline=out/'baseline';baseline.mkdir();hashes={}
    for name in ['src','shaders','scripts','cmake']:
        shutil.copytree(ROOT/name,baseline/name)
    for name in ['CMakeLists.txt','CMakePresets.json','README.md','DYNAMICS.md','WINDOW_SOLVER.md']:
        shutil.copy2(ROOT/name,baseline/name)
    for folder in ['src','shaders','scripts','cmake','assets']:
        for f in (ROOT/folder).rglob('*'):
            if f.is_file():hashes[str(f.relative_to(ROOT))]=hashlib.sha256(f.read_bytes()).hexdigest()
    runtime=baseline/'runtime';runtime.mkdir()
    exe=ROOT/'build/vs2022/bin/Release/HairLabDX12.exe';shutil.copy2(exe,runtime/exe.name)
    for folder in ['assets','shaders']:shutil.copytree(exe.parent/folder,runtime/folder)
    (baseline/'hashes.json').write_text(json.dumps(hashes,indent=2))
    fixtures={'declaredBeforeTuning':True,'h':1/120,'massAndStiffness':'unchanged native GuideState','gravity':'world (0,-9.81,0) through staged fixed neck inverse rotation','trainStrandIndices':[0,1,5,9],'heldOutStrandIndices':[17,127,511,1023],'seeds':[31871,912773],'analyticVertices':[2,5,32],'gates':{'strain':.05,'trajectoryRMSMeters':.0001,'trajectoryMaxMeters':.001,'secondsAll':5,'maxSweeps':32},'referenceQualification':'residual and refinement, never iteration count alone'}
    (out/'FIXTURES.json').write_text(json.dumps(fixtures,indent=2))
    state={'runId':runid,'status':'active','maxCycles':12,'cycles':[],'adaptiveInitializationInitiallyFound':False,'instructions':'No AGENTS.md found in project root or source/shader/script/cmake subtrees; Unity is never accessed. Baseline source/runtime copies preserve existing user content.','classification':None}
else:
    runid=marker.read_text().strip();out=ROOT/'artifacts/autonomous'/runid;state=json.loads((out/'STATUS.json').read_text())
    if a.action=='start':
        if len(state['cycles'])>=12:raise SystemExit('Cycle limit reached')
        state['cycles'].append({'cycle':len(state['cycles'])+1,'hypothesis':a.hypothesis,'state':'running'})
    elif a.action=='finish':
        state['cycles'][-1].update(state='finished',result=a.result,decision=a.decision)
        snapshot=out/f"cycle-{len(state['cycles']):02d}"/'source'
        snapshot.mkdir(parents=True,exist_ok=True)
        for folder in ['src','shaders','cmake','scripts']:shutil.copytree(ROOT/folder,snapshot/folder,dirs_exist_ok=True)
        shutil.copy2(ROOT/'CMakeLists.txt',snapshot/'CMakeLists.txt')
    elif a.result:state['status']=a.result
state['updatedUTC']=datetime.datetime.now(datetime.timezone.utc).isoformat()
(out/'STATUS.json').write_text(json.dumps(state,indent=2)+'\n')
text='# Autonomous experiments\n\nBaseline, fixtures and immutable source hashes are saved before tuning. Maximum 12 cycles.\n'
for c in state['cycles']:
    text+=f"\n## Cycle {c['cycle']}: {c['state']}\n\nHypothesis: {c['hypothesis']}\n\nResult: {c.get('result','pending')}\n\nDecision: {c.get('decision','pending')}\n"
(out/'EXPERIMENTS.md').write_text(text)
(ROOT/'AUTONOMOUS_PLAN.md').write_text(f'''# Autonomous convergence work

Run: `{runid}`; status: {state['status']}; cycles: {len(state['cycles'])}/12.

1. Preserve baseline/user changes and declare analytic/train/held-out fixtures.
2. Audit derivatives, units, history, pins, cache and barriers; reproduce baseline strain.
3. Compare FullPrediction/InertialOnly/AdaptiveAcceleration on identical targets and frozen starts.
4. Test one same-energy acceleration/schedule change per cycle; keep/revert using objective, residual, motion and timings.
5. Qualify double small-problem references by residual/refinement, test held-out trajectories and all-strand 5s stress.
6. Run full Debug/Release regressions only for finalists; save best runtime and Korean REPORT.md.

Current hypothesis: {state['cycles'][-1]['hypothesis'] if state['cycles'] else 'Baseline capture and audit'}

Ledger: `artifacts/autonomous/{runid}/EXPERIMENTS.md`; machine state: `STATUS.json`.
Never alter model/material/gravity/pins/time or historical expectations to pass a gate. No Unity access.
''')
print(out)
