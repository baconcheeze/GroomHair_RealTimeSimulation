"""Explicit bounded hardware tests; all inputs are project-local. Never reads Unity."""
import argparse, hashlib, json, pathlib, shutil, subprocess
p=argparse.ArgumentParser()
p.add_argument('--config',choices=['Debug','Release'],default='Debug')
p.add_argument('--build-dir',default='build/vs2022',help='CMake binary directory, relative to project root')
a=p.parse_args()
root=pathlib.Path(__file__).resolve().parents[1]
exe=root/a.build_dir/f'bin/{a.config}/HairLabDX12.exe'
out=root/f'artifacts/window/{a.config}';out.mkdir(parents=True,exist_ok=True)
common=['--scene','hair','--view','front','--width','1024','--height','1024','--frames','130']
def run(name,options,program=exe,cwd=root):
    subprocess.run([str(program),*map(str,options),'--log',str(out/f'{name}.log')],cwd=cwd,check=True,timeout=240)
    print('PASS hardware GPU:',name,flush=True)
for test in ['position-local','window-solver','window-groom']:
    run(test,[*common,'--test',test,'--report',out/f'{test}.json'])
def capture(name,*options):
    run(name,[*common,'--draw','guides','--participating','all','--window-target','rest',
        '--capture',out/f'{name}.png','--report',out/f'{name}.json',*options])
capture('rest')
capture('joint-perturbed','--window-perturb','joint')
for n in [1,8,32,128]:capture(f'relaxed-{n}','--window-perturb','joint','--window-sweeps',n)
capture('reset','--window-perturb','joint','--window-sweeps','128','--window-reset-after','--validate-rest')
assert (out/'rest.png').read_bytes()==(out/'reset.png').read_bytes(),'Exact reset image mismatch'
assert (out/'rest.png').read_bytes()!=(out/'joint-perturbed.png').read_bytes(),'Guide movement invisible'
assert (out/'joint-perturbed.png').read_bytes()!=(out/'relaxed-128.png').read_bytes(),'Relaxation invisible'
for name,frames in [('idle-before',1),('idle-after',130)]:
    run(name,[*common,'--frames',frames,'--capture',out/f'{name}.png'])
assert (out/'idle-before.png').read_bytes()==(out/'idle-after.png').read_bytes(),'Idle image changed'
old=root/f'artifacts/{a.config}/front.png'
if old.exists():assert old.read_bytes()==(out/'idle-before.png').read_bytes(),'Historical static image changed'
run('queued-auto',[*common,'--draw','guides','--window-target','displaced','--window-auto',
    '--validate-rest','--report',out/'queued-auto.json','--capture',out/'queued-auto.png'])
auto=json.loads((out/'queued-auto.json').read_text())
assert auto['ProblemGeneration']==1 and auto['RequestedSweepsSincePrepare']==130
for field in ['restChanges','targetChanges','velocityChanges','inactiveChanges','failures']:assert auto[field]==0
package=root/f'build/window-isolated-{a.config}';package.mkdir(parents=True,exist_ok=True)
shutil.copy2(exe,package/exe.name)
for folder in ['assets','shaders']:shutil.copytree(exe.parent/folder,package/folder,dirs_exist_ok=True)
run('isolated',[*common,'--window-target','displaced','--window-perturb','joint','--window-sweeps','32',
    '--report',out/'isolated.json','--capture',out/'isolated.png','--validate-rest'],package/exe.name,out)
# Invalid numerical data/stale/unprepared execution are tested inside window-solver.
for record in json.loads((root/'assets/manifest.json').read_text())['files']:
    if 'staged' in record:
        assert hashlib.sha256((root/record['staged']).read_bytes()).hexdigest()==record['stagedSHA256']
manifest=json.loads((root/'references/orientation/manifest.json').read_text())
for record in manifest['records']:
    if 'copied' in record:assert hashlib.sha256((root/record['copied']).read_bytes()).hexdigest()==record['sha256']
for helper in ['GuideQuaternion.hlsl','GuideOrientationLocal.hlsl']:
    assert (root/f'shaders/orientation/{helper}').read_bytes()==(root/f'references/orientation/Shaders/{helper}').read_bytes()
hashes={str(f.relative_to(root)):hashlib.sha256(f.read_bytes()).hexdigest()
    for folder in [root/'assets',root/'shaders/window',root/'src'] for f in folder.rglob('*')
    if f.is_file() and (folder.name!='src' or f.name.startswith('Window'))}
shader_hashes={f.name:hashlib.sha256(f.read_bytes()).hexdigest() for f in (exe.parent/'shaders/window').glob('*') if f.is_file()}
summary={'config':a.config,'hardwareGPU':True,'passed':True,'sourceTreeRead':False,
    'sharedCurrentPosition':True,'idleFrames':130,'idleImageInvariant':True,'resetImageExact':True,
    'historicalStaticImageCompared':old.exists(),'isolatedDifferentCwd':True,
    'autoProblemGeneration':auto['ProblemGeneration'],'autoSweeps':auto['RequestedSweepsSincePrepare'],
    'manualInputResizeTested':False,'imageContentsInspected':False,'hashes':hashes,'compiledShaderHashes':shader_hashes}
(out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
print(json.dumps({k:v for k,v in summary.items() if k not in ['hashes','compiledShaderHashes']},indent=2))
