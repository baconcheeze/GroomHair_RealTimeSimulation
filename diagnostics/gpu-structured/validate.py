"""Offline native-test launcher. Does not change PowerShell execution policy.
GPU solve and deployable executable are entirely native; this is optional automation.
"""
import pathlib,subprocess,json,sys,hashlib
root=pathlib.Path(__file__).resolve().parents[2];config=sys.argv[1] if len(sys.argv)>1 else 'Release';assert config in ['Release','Debug'];run=pathlib.Path(sys.argv[2]) if len(sys.argv)>2 else pathlib.Path((root/'build/gpu-structured-run.txt').read_text().strip());run=run if run.is_absolute() else root/run;out=run/('final-'+config);out.mkdir(parents=True,exist_ok=True);bin=root/'build/gpu-structured/bin'/config
inputs=root/'artifacts/ls-refinement/20261030T152921Z/suite-inputs';small=root/'artifacts/qr-nullspace/20261030T145943Z/frozen';problems=root/'artifacts/strand-block/20261030T141357Z/inputs';records=[]
def test(name,exe,args,codes=(0,)):
 with (out/(name+'.log')).open('w',encoding='utf-8') as f:r=subprocess.run([str(exe),*map(str,args)],cwd=root,stdout=f,stderr=subprocess.STDOUT,timeout=180)
 records.append({'test':name,'exit':r.returncode});print(config,name,'exit',r.returncode,flush=True)
 if r.returncode not in codes:raise RuntimeError(f'{name} failed; inspect {out/name}.log')
test('probe',bin/'GpuStructuredProbe.exe',[inputs,out/'probe'])
for name,data,mode,codes in [('small',small,'replay',(0,)),('g64',inputs,'replay',(0,)),('formation',inputs,'formation',(0,)),('mixed',inputs,'mixed',(0,1)),('hybrid5',problems,'hybrid5',(0,)),('hybrid1',problems,'hybrid1',(0,)),('dynamics16',problems,'dynamics',(0,))]:test(name,bin/'GpuStructuredLSCandidate.exe',[mode,data,out/name],codes)
if config=='Release':test('benchmark',bin/'GpuStructuredLSCandidate.exe',['benchmark',inputs,out/'benchmark'])
(out/'execution.json').write_text(json.dumps(records,indent=2));hashes={str(p.relative_to(bin)):hashlib.sha256(p.read_bytes()).hexdigest() for p in bin.rglob('*') if p.is_file() and(p.suffix=='.dxil' or p.name.startswith('GpuStructured') and p.suffix=='.exe')};(out/'executed-binary-hashes.json').write_text(json.dumps(hashes,indent=2))
