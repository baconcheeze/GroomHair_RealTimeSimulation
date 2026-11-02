"""Evidence reduction only. No solver execution or threshold/parameter tuning."""
from pathlib import Path
import csv,json,hashlib,shutil,struct
root=Path(__file__).resolve().parents[2]
out=root/'artifacts/joint-window'/(root/'build/joint-run.txt').read_text().strip()
def read(p):return json.loads(p.read_text(encoding='utf-8'))
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
baseline=read(out/'inputs/baseline-reproduced.json')
rows=[];final=[]
for path in [*sorted((out/'primary').glob('*/curve.json')),*sorted((out/'checks').glob('*/*/curve.json'))]:
    data=read(path);case='primary' if path.parent.parent.name=='primary' else path.parent.parent.name
    for c in data['checkpoints']:
        w=c['work'];e=c['physical'];v=c['tipDisplacement'];err=c['tipError'];ref=[v[k]-err[k] for k in range(3)]
        row={'case':case,'method':data['method'],'width':data['width'],'macro':c['macroSweeps'],'sourceId':data['sourceId'],'referenceQualified':data['referenceQualified'],
             'F':e['TotalObjective'],'gapJ':c['objectiveGapJ'],'RMSmeters':c['positionRMS'],'maxMeters':c['positionMax'],'positionCorrection':e['MaxPositionCorrectionMeters'],'qStationarity':e['NormalizedQuaternionStationarity'],'qSpectral':e['NormalizedSpectralViolation'],'qMaxRadians':c['maxQuaternionRadians'],'qRMSRadians':c['rmsQuaternionRadians'],'axialStrain':c['maxAxialStrain'],'SSVectorError':c['maxSSVectorError'],
             **{f'tip{a}':v[k] for k,a in enumerate('XYZ')},**{f'tipError{a}':err[k] for k,a in enumerate('XYZ')},**{f'referenceTip{a}':ref[k] for k,a in enumerate('XYZ')},
             'referenceDirectedFraction':sum(v[k]*ref[k] for k in range(3))/sum(a*a for a in ref) if sum(a*a for a in ref)>0 else None,**w}
        row['fullObjectiveEvaluationsDerived']=w['linearizations']+w['trials'] if data['method']!='sequential' else None
        rows.append(row)
        if c['macroSweeps']==32:final.append(row)
with (out/'error-versus-work.csv').open('w',newline='',encoding='utf-8-sig') as f:
    writer=csv.DictWriter(f,list(rows[0]));writer.writeheader();writer.writerows(rows)
text=['# Recorded curves (instrumented CPU wall; not GPU timing)','', '| case | method | width | RMS µm @32 | gap J | Jacobians | solves | active DOFs | CPU ms |','|---|---|---:|---:|---:|---:|---:|---:|---:|']
for c in final:text.append(f"|{c['case']}|{c['method']}|{c['width']}|{c['RMSmeters']*1e6:.6g}|{c['gapJ']:.6g}|{c['linearizations']}|{c['linearSolves']}|{c['activeDOFsProcessed']}|{c['cpuMilliseconds']:.4g}|")
(out/'TABLES.md').write_text('\n'.join(text)+'\n',encoding='utf-8')
verification=read(out/'verification-final/jacobian-and-coverage.json')
primary=[r for r in final if r['case']=='primary'];joint=[r for r in primary if r['method']=='joint']
target=read(out/'PREDECLARED.json')['screeningTargetRMSMeters']
qualified=all(x['referenceQualified'] for x in final)
passed=any(r['RMSmeters']<=target for r in rows if r['case']=='primary' and r['method']=='joint')
result={'classification':'no meaningful improvement' if not passed and qualified else 'implementation/reference issue not resolved' if not qualified else 'larger-block dependence' if min(x['RMSmeters'] for x in joint if x['width']==2)>target else 'promising small-block correction',
        'gpuPort':'NO-GO' if not passed or not qualified else 'requires further bounded validation; not automatically authorized',
        'screeningTargetRMSMeters':target,'screeningTargetMet':passed,'allEvaluatedReferenceCasesQualified':qualified,'primary32':primary,
        'FDmaxNormalizedError':max(v['maxNormalizedJacobianError'] for v in verification),'FDscalarComparisons':sum(v['samples'] for v in verification),'coveragePassed':all(v['coveragePassed'] for v in verification),
        'policyChangedAfterCurves':False,'GPUWorkPerformed':False,'fiveSecondGroomSuiteRerun':False,'baselineReproduced':baseline}
before=read(out/'preserved-before.json');assert all(sha(root/p)==h for p,h in before.items())
inputs=read(out/'input-hashes.json');assert all(sha(out/p)==h for p,h in inputs.items())
old=root/'artifacts/autonomous/20261029T173940Z';archived=read(old/'runtime-hashes.json');assert all(sha(old/'best-tested-runtime'/p)==h for p,h in archived.items())
result['productionAndCandidateUnchanged']=True;result['frozenInputsUnchanged']=True
(out/'RESULT.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
refs=[]
for file in sorted((out/'inputs').glob('*.jwp')):
    b=file.read_bytes();header=struct.unpack('<9I',b[:36]);ref=struct.unpack('<3d',b[48:72]);refs.append({'case':file.stem,'sourceId':header[2],'substep':header[3],'initialIterate':header[4],'qualified':bool(header[8]),'positionCorrection':ref[0],'qStationarity':ref[1],'additionalRefinementMaxPosition':ref[2]})
(out/'references.json').write_text(json.dumps(refs,indent=2))
manifest={str(p.relative_to(root)):sha(p) for p in (root/'diagnostics/joint').rglob('*') if p.is_file()}
for folder in ['src','shaders']:
    for p in (root/folder).rglob('*'):
        if p.is_file():manifest[str(p.relative_to(root))]=sha(p)
manifest['build/joint-cpu/bin/Release/JointPrototype.exe']=sha(root/'build/joint-cpu/bin/Release/JointPrototype.exe')
(out/'source-and-binary-hashes.json').write_text(json.dumps(manifest,indent=2))
print(result['classification'],result['gpuPort'],'screen met:',passed,'production/input hashes unchanged')
raise SystemExit(0 if passed else 1)
