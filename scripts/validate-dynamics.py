"""Bounded hardware dynamics, old solver regressions and app-owned capture checkpoints.
All outputs go below artifacts/dynamics; historical validation artifacts are read-only.
"""
import argparse,hashlib,json,pathlib,shutil,subprocess
p=argparse.ArgumentParser();p.add_argument('--config',choices=['Debug','Release'],default='Debug');p.add_argument('--build-dir',default='build/vs2022');p.add_argument('--skip-regression',action='store_true');p.add_argument('--output-root',default='',help='Explicit new result directory; defaults remain unchanged');p.add_argument('--output-subdir',default='',help='Optional new artifact subdirectory; preserve historical runs');a=p.parse_args()
root=pathlib.Path(__file__).resolve().parents[1];exe=root/a.build_dir/f'bin/{a.config}/HairLabDX12.exe'
out=(root/a.output_root if a.output_root else root/f'artifacts/dynamics/{a.config}'/a.output_subdir);out.mkdir(parents=True,exist_ok=True)
def run(name,args,program=exe,cwd=root,expect=True):
    log=out/f'{name}.log';log.parent.mkdir(parents=True,exist_ok=True)
    result=subprocess.run([str(program),*map(str,args),'--log',str(log)],cwd=cwd,timeout=240,capture_output=True)
    assert (result.returncode==0)==expect,(name,result.returncode,log.read_text()[-2500:] if log.exists() else '')
    print('PASS',name,flush=True)
for test in ['dynamics-local','dynamics-groom']:
    run(test,['--test',test,'--frames','130','--report',out/f'{test}.json'])
if not a.skip_regression:
    for test in ['guide-state','orientation-local','orientation-groom','position-local','window-solver','window-groom']:
        run('regression/'+test,['--test',test,'--frames','130','--report',out/'regression'/f'{test}.json'])
common=['--scene','hair','--view','front','--width','1024','--height','1024','--draw','guides','--solver-mode','dynamics','--participating','all']
def capture(name,steps,*extra,frames=1):
    run(name,[*common,'--sim-capture-step',steps,'--frames',frames,'--capture',out/f'{name}.png','--report',out/f'{name}.json',*extra])
for n in [0,1,8,16,32]:capture(f'step-{n}',n,'--sim-diagnostics')
capture('step-32-idle',32,frames=130)
assert (out/'step-32.png').read_bytes()==(out/'step-32-idle.png').read_bytes(),'Rendered frames changed physical step count/state'
assert json.loads((out/'step-32-idle.json').read_text())['completedSubsteps']==32
capture('reset',32,'--sim-reset-after')
assert (out/'reset.png').read_bytes()==(out/'step-0.png').read_bytes(),'Reset does not restore static guide image'
capture('replay',32)
assert (out/'replay.png').read_bytes()==(out/'step-32.png').read_bytes(),'Deterministic replay differs'
capture('velocity-only',16,'--gravity','0,0,0','--sim-velocity','0.01,0.02,0.005')
run('static-idle',['--scene','hair','--view','front','--width','1024','--height','1024','--frames','130','--capture',out/'static-idle.png','--validate-rest','--report',out/'static-idle.json'])
old=root/f'artifacts/{a.config}/front.png'
if old.exists():assert old.read_bytes()==(out/'static-idle.png').read_bytes(),'Historical static preview changed'
run('bootstrap',['--scene','bootstrap','--frames','120','--capture',out/'bootstrap.png'])
package=root/f'build/dynamics-isolated-{a.config}';package.mkdir(parents=True,exist_ok=True);shutil.copy2(exe,package/exe.name)
for folder in ['assets','shaders']:shutil.copytree(exe.parent/folder,package/folder,dirs_exist_ok=True)
run('isolated',[*common,'--sim-steps','8','--frames','2','--report',out/'isolated.json','--capture',out/'isolated.png'],package/exe.name,out)
# World gravity must be mapped through the fixed asset frame, independently of view.
reference=json.loads((out/'step-8.json').read_text())
assert reference['gravityWorld']==[0,-9.81,0]
assert abs(sum(v*v for v in reference['gravitySolver'])**.5-9.81)<3e-6
for view in ['side','back']:
    run('gravity-'+view,[*common,'--view',view,'--sim-steps','8','--frames','1','--report',out/f'gravity-{view}.json','--capture',out/f'gravity-{view}.png'])
    report=json.loads((out/f'gravity-{view}.json').read_text())
    assert report['gravitySolver']==reference['gravitySolver'],'Camera changed solver gravity'
for i,args in enumerate([
    ['--scene','bootstrap','--sim-steps','1'],
    ['--solver-mode','window','--sim-steps','1'],
    ['--sim-h','0'],
    ['--sim-steps','1','--window-target','rest'],
    ['--test','dynamics-local','--sim-steps','1'],
    ['--sim-steps','1','--gravity','nan,0,0'],
]):run(f'negative/{i}',args,expect=False)
# Original staged files and robust math helper copies stay unchanged. Do not read source paths.
for record in json.loads((root/'assets/manifest.json').read_text())['files']:
    if 'staged' in record:assert hashlib.sha256((root/record['staged']).read_bytes()).hexdigest()==record['stagedSHA256']
for helper in ['GuideQuaternion.hlsl','GuideOrientationLocal.hlsl']:
    assert (root/f'shaders/orientation/{helper}').read_bytes()==(root/f'references/orientation/Shaders/{helper}').read_bytes()
def function(source,name):
    start=source.index('bool '+name+'(');brace=source.index('{',start);depth=1;end=brace+1
    while depth:
        if source[end]=='{':depth+=1
        elif source[end]=='}':depth-=1
        end+=1
    return source[start:end]
old_window=root/'build/isolated-Debug/shaders/window/WindowSolve.hlsl'
schedule_compared=False
if old_window.exists():
    before=old_window.read_text();after=(root/'shaders/window/WindowSolve.hlsl').read_text()
    for name in ['P','Q','CachedSweep','UncachedSweep']:assert function(before,name)==function(after,name),name
    schedule_compared=True
hashes={str(f.relative_to(root)):hashlib.sha256(f.read_bytes()).hexdigest() for folder in [root/'src',root/'shaders/dynamics',root/'shaders/window'] for f in folder.glob('*') if f.is_file() and (folder.name!='src' or f.name.startswith('Dynamics'))}
summary={'config':a.config,'passed':True,'hardwareGpu':True,'historicalRegressionExecuted':not a.skip_regression,'staticCaptureUnchanged':old.exists(),'resetReplayExact':True,'stepCountIndependentOfFrames':True,'isolatedRuntimeDifferentCwd':True,'sourceAssetsAndRobustHelpersUnchanged':True,'windowPQScheduleComparedToPreDynamicsCopy':schedule_compared,'manualUiMouseResizeTested':False,'pngFilesWritten':True,'imageContentsInspected':False,'sourceTreeRead':False,'hashes':hashes}
(out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps({k:v for k,v in summary.items() if k!='hashes'},indent=2))
