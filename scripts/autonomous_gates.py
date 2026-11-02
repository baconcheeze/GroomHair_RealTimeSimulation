"""Engineering gates are intentionally strict and separate from finite-state smoke tests."""
import pathlib,json,math
root=pathlib.Path(__file__).resolve().parents[1];run=(root/'build/autonomous-run.txt').read_text().strip();r=root/'artifacts/autonomous'/run;v=r/'cycle-12/Release';f=r/'cycle-12/final-checks'
def read(p):return json.loads(p.read_text())
def logtext(p):
 b=p.read_bytes();return b.decode('utf-16' if b.startswith(b'\xff\xfe') else 'utf-8-sig')
def finite(x):
 if isinstance(x,dict):return all(finite(v) for v in x.values())
 if isinstance(x,list):return all(finite(v) for v in x)
 return not isinstance(x,(float,int)) or isinstance(x,bool) or math.isfinite(x)
gates={};gates['1_existing_regressions']=all(read(r/f'cycle-12/{c}/regression/summary.json')['passed'] and '100% tests passed' in logtext(r/f'cycle-12/ctest-{c}.log') for c in ['Debug','Release'])
checks=[read(v/(name+'.json')) for name in ['all-rest-600','all-kick-31871-600','all-kick-912773-600']]
gates['2_all_strands_finite_invariants']=all(j['strands']==2839 and j['steps']==600 and finite(j) and all(c['diagnostics']['failures']==0 and c['diagnostics']['rootPositionError']==0 and c['diagnostics']['rootQuaternionError']==0 and c['diagnostics']['restChanges']==0 and c['diagnostics']['inactiveChanges']==0 for c in j['checkpoints']) for j in checks)
gates['3_max_axial_strain_5percent']=checks[0]['maxAbsoluteAxialStrainOverSteps']<=.05
names=['candidate-train','candidate-heldout',*[f'candidate-{s}-kick-{seed}' for s in ['train','heldout'] for seed in [31871,912773]]]
trajectory={name:{'rms':read(v/(name+'.json'))['trajectoryRMS'],'max':read(v/(name+'.json'))['trajectoryMax']} for name in names}
qualified=all(read(r/f'cycle-02/refinement-{s}.json')['qualified'] and read(r/f'cycle-02/reference-{s}-refined.json')['qualified'] for s in ['train','heldout']) and all(read(p)['qualified'] for p in v.glob('qualification-*.json')) and len(list(v.glob('qualification-*.json')))==4
gates['4_qualified_reference_trajectory']=qualified and all(j['rms']<=.0001 and j['max']<=.001 for j in trajectory.values())
base=read(f/'frozen-0-cached.json');best=read(f/'frozen-3-cached.json');paired=list(zip(base['checkpoints'],best['checkpoints']))
gates['5_frozen_objective_residual_improved']=all(y['energy']['TotalObjective']<x['energy']['TotalObjective'] and y['energy']['MaxPositionCorrectionMeters']<=x['energy']['MaxPositionCorrectionMeters'] and y['energy']['NormalizedQuaternionStationarity']<=x['energy']['NormalizedQuaternionStationarity'] for x,y in paired if x['sweeps'] in [32,512])
result={'classification':'validated' if all(gates.values()) else 'improved-but-incomplete','gates':gates,'strainLimit':.05,'actualPeakStrain':checks[0]['maxAbsoluteAxialStrainOverSteps'],'trajectoryThresholdMeters':{'rms':.0001,'max':.001},'trajectory':trajectory,'referenceQualified':qualified,'notGlobalMinimumProof':True}
(r/'GATES.json').write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2));raise SystemExit(0 if all(gates.values()) else 1)
