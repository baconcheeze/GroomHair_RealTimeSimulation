"""Explicit hardware-GPU orientation tests and app-owned PNG regressions."""
import argparse,csv,hashlib,json,pathlib,re,shutil,subprocess
p=argparse.ArgumentParser();p.add_argument('--config',choices=['Debug','Release'],default='Debug');p.add_argument('--offline',action='store_true',help='Validate project-local snapshots only; never read the Unity tree');args=p.parse_args()
root=pathlib.Path(__file__).resolve().parents[1];exe=root/f'build/vs2022/bin/{args.config}/HairLabDX12.exe'
out=root/f'artifacts/orientation/{args.config}';out.mkdir(parents=True,exist_ok=True)
def run(name,options,program=exe,cwd=root):
    subprocess.run([str(program),*map(str,options),'--log',str(out/f'{name}.log')],cwd=cwd,check=True,timeout=180)
    print('PASS HARDWARE GPU',name,flush=True)
common=['--scene','hair','--view','front','--width','1024','--height','1024','--frames','130']
for test in ['guide-state','orientation-local','orientation-groom']:
    cmd=[*common,'--test',test,'--report',out/f'{test}.json']
    if test=='guide-state':cmd+=['--capture',out/'idle.png']
    run(test,cmd)
def capture(name,*options):run(name,[*common,'--capture',out/f'{name}.png',*options])
capture('baseline','--frames','1')
capture('perturbed-lines','--orientation-perturb','bend')
capture('frames-perturbed','--orientation-perturb','bend','--material-frames','--frame-stride','1')
capture('frames-relaxed','--orientation-perturb','bend','--orientation-sweeps','512','--material-frames','--frame-stride','1')
capture('after-reset','--orientation-perturb','bend','--orientation-sweeps','512','--orientation-reset-after','--validate-rest','--report',out/'after-reset-rest.json')
run('queued-auto',[*common,'--orientation-perturb','bend','--orientation-auto','--material-frames','--frame-stride','1','--capture',out/'queued-auto.png','--validate-rest','--report',out/'queued-auto-rest.json'])
baseline=(out/'baseline.png').read_bytes()
for name in ['idle','perturbed-lines','after-reset']:assert (out/f'{name}.png').read_bytes()==baseline,f'Image invariance: {name}'
assert (out/'frames-perturbed.png').read_bytes()!=baseline,'Material frame overlay must be visible'
assert (out/'frames-perturbed.png').read_bytes()!=(out/'frames-relaxed.png').read_bytes(),'Material axes must relax'
old=root/f'artifacts/{args.config}/front.png'
historical=old.exists()
if historical:assert old.read_bytes()==baseline,'Static preview differs from previous task capture'
print('PASS idle / perturbed lines / reset PNG invariance; changed material-frame axes',flush=True)
package=root/f'build/orientation-isolated-{args.config}';package.mkdir(parents=True,exist_ok=True)
shutil.copy2(exe,package/exe.name)
for folder in ['assets','shaders']:shutil.copytree(exe.parent/folder,package/folder,dirs_exist_ok=True)
run('isolated-local',[*common,'--test','orientation-local','--report',out/'isolated-local.json'],package/exe.name,out)
# Current files must still match read-only source snapshots; no Unity execution.
manifest=json.loads((root/'references/orientation/manifest.json').read_text())
for record in manifest['records']:
    if 'source' in record:
        if not args.offline:assert hashlib.sha256(pathlib.Path(record['source']).read_bytes()).hexdigest()==record['sha256']
        assert hashlib.sha256((root/record['copied']).read_bytes()).hexdigest()==record['sha256']
for helper in ['GuideQuaternion.hlsl','GuideOrientationLocal.hlsl']:
    assert (root/f'shaders/orientation/{helper}').read_bytes()==(root/f'references/orientation/Shaders/{helper}').read_bytes()
guide_reference=(root/'references/orientation/historical/guide-state.txt').read_text()
expected_counts=re.search(r'strands=(\d+), vertices=(\d+), segments=(\d+), bends=(\d+)',guide_reference)
assert expected_counts
guide=json.loads((out/'guide-state.json').read_text())
assert tuple(guide[k] for k in ['strands','vertices','segments','bends'])==tuple(map(int,expected_counts.groups()))
expected_lengths=re.search(r'min=([^,]+), max=([^,]+), average=([^,]+),',guide_reference)
assert expected_lengths
for field,value in zip(['minLength','maxLength','averageLength'],map(float,expected_lengths.groups())):
    assert abs(guide['initialization'][field]-value)<1e-8
local=list(csv.DictReader((out/'orientation-local.csv').open()))
historical_cases=list(csv.DictReader((root/'references/orientation/historical/local-cases.csv').open()))
assert len(local)==len(historical_cases)==375
max_before=max_reference=0
for native,unity in zip(local,historical_cases):
    # Generator order and float-rounded frozen local data are the source suite's.
    for key in ['energy_before','reference_energy']:
        a,b=float(native[key]),float(unity[key]);error=abs(a-b)/max(1,abs(b))
        assert error<1e-12,(key,native['case'],error)
        if key=='energy_before':max_before=max(max_before,error)
        else:max_reference=max(max_reference,error)
hashes={}
for path in sorted((exe.parent/'shaders/orientation').glob('*')):
    if path.is_file():hashes[path.name]=hashlib.sha256(path.read_bytes()).hexdigest()
result={'hardwareGpu':True,'config':args.config,'allTestsPassed':True,'idleFrames':130,'staticCaptureInvariance':True,'historicalStaticPngCompared':historical,'sourceFilesUnchanged':True,'pureMathHelpersByteIdentical':True,'localCasesActuallyExecuted':len(local),'historicalFrozenCaseInitialEnergyMaxRelativeError':max_before,'historicalReferenceEnergyMaxRelativeError':max_reference,'localFixtureInputSHA256':hashlib.sha256((out/'orientation-local.inputs.bin').read_bytes()).hexdigest(),'shaderHashes':hashes,'isolatedRuntimeDifferentCwd':True,'newUnityExecution':False,'manualUiInputResizeTested':False}
if args.offline:result['sourceFilesUnchanged']=None;result['sourceTreeRead']=False
other=root/'artifacts/orientation'/('Release' if args.config=='Debug' else 'Debug')/'orientation-local.inputs.bin'
if other.exists():
    assert other.read_bytes()==(out/'orientation-local.inputs.bin').read_bytes()
    result['crossConfigurationFixtureInputsIdentical']=True
(out/'summary.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result,indent=2),flush=True)
