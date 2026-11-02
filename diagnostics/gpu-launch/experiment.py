"""Bounded offline launcher. The native solve has no Python dependency."""
from pathlib import Path
import argparse,datetime,hashlib,json,shutil,subprocess
ROOT=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('--phase',choices=['packing','accuracy'],required=True);p.add_argument('--run',required=True);args=p.parse_args();out=Path(args.run).resolve();out.mkdir(parents=True,exist_ok=True)
if not (out/'preserved-before.json').exists():
 files=[]
 for name in ['src','shaders','assets','diagnostics/gpu-structured','diagnostics/native-ls']:files.extend(f for f in (ROOT/name).rglob('*') if f.is_file())
 files.extend(ROOT.glob('*.json'));files.append(ROOT/'CMakeLists.txt')
 (out/'preserved-before.json').write_text(json.dumps({str(f.relative_to(ROOT)):hashlib.sha256(f.read_bytes()).hexdigest() for f in files},indent=2))
 (out/'PREDECLARED.json').write_text(json.dumps({'warmups':2,'samples':11,'groups':[1,32,64],'tiles':[100,512,2839],'distribution':['primary9852','source14','source59'],'selection':'same staged measured-median policy; no numerical change'},indent=2))
inputs=ROOT/'artifacts/ls-refinement/20261030T152921Z/suite-inputs';problems=ROOT/'artifacts/strand-block/20261030T141357Z/inputs'
def invoke(cmd,dest,timeout=600,cwd=ROOT):
 dest.mkdir(parents=True,exist_ok=True)
 with (dest/'run.log').open('w') as f:r=subprocess.run(list(map(str,cmd)),cwd=cwd,stdout=f,stderr=subprocess.STDOUT,timeout=timeout)
 (dest/'invocation.json').write_text(json.dumps({'command':list(map(str,cmd)),'cwd':str(cwd),'exit':r.returncode},indent=2));print(dest.name,r.returncode,flush=True)
 if r.returncode:raise RuntimeError((dest/'run.log').read_text(errors='replace')[-3000:])
def stage(config):
 invoke(['cmake','--build',ROOT/'build/gpu-launch','--config',config,'--target','G64LaunchCandidate','--','/m:1','/v:m'],out/f'build-{config}')
 source=ROOT/'build/gpu-launch/bin'/config; runtime=out/'runtime'/config;runtime.mkdir(parents=True,exist_ok=True);shutil.copy2(source/'G64LaunchCandidate.exe',runtime)
 shutil.copytree(source/'shaders',runtime/'shaders',dirs_exist_ok=True)
 for name in ['LICENSE','COPYING','README.HairLab.md']:shutil.copy2(ROOT/'third_party/qd-2.3.24'/name,runtime/('QD-'+name))
 hashes={str(f.relative_to(runtime)):hashlib.sha256(f.read_bytes()).hexdigest() for f in runtime.rglob('*') if f.is_file()}
 (out/f'executed-{config}-hashes.json').write_text(json.dumps(hashes,indent=2));return runtime/'G64LaunchCandidate.exe'
def run(exe,mode,inp,dest,group=1,tile=100,corrections=1):
 # Recheck every executed binary/DXIL against this successful build's manifest.
 hashes=json.loads((out/f'executed-{exe.parent.name}-hashes.json').read_text())
 for f,h in hashes.items():assert hashlib.sha256((exe.parent/f).read_bytes()).hexdigest()==h
 invoke([exe,mode,inp,dest,group,tile,corrections],dest,cwd=exe.parent)
if args.phase=='packing':
 rel=stage('Release');dbg=stage('Debug')
 run(rel,'ownership',inputs,out/'ownership-Release');run(dbg,'ownership',inputs,out/'ownership-Debug')
 run(rel,'benchmark',inputs,out/'study')
else:
 selection=json.loads((out/'study/SELECTION.json').read_text());g,t=selection['group'],selection['tile']
 for config in ['Release','Debug']:
  exe=stage(config)
  # Self-contained input package, used from an unrelated executable-relative working directory.
  target=exe.parent/'inputs';shutil.copytree(inputs,target/'linear',dirs_exist_ok=True);shutil.copytree(problems,target/'nonlinear',dirs_exist_ok=True)
  run(exe,'replay',target/'linear',out/config/'replay',g,t)
  run(exe,'formation',target/'linear',out/config/'formation',g,t)
  run(exe,'hybrid1',target/'nonlinear',out/config/'hybrid1',g,t)
  run(exe,'dynamics',target/'nonlinear',out/config/'dynamics16-c1',g,t,1)
