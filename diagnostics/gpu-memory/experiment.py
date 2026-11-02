"""Offline bounded launcher. Native solves never invoke Python."""
from pathlib import Path
import argparse,subprocess,json,hashlib,shutil
ROOT=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('phase',choices=['preflight','benchmark','final']);p.add_argument('--run',required=True);a=p.parse_args();R=Path(a.run).resolve();R.mkdir(parents=True,exist_ok=True)
if not (R/'preserved-before.json').exists():
 files=[]
 for name in ['src','shaders','assets','diagnostics/gpu-launch','diagnostics/gpu-structured','diagnostics/native-ls']:files.extend(f for f in (ROOT/name).rglob('*') if f.is_file())
 files += [ROOT/'CMakeLists.txt',ROOT/'CMakePresets.json']
 (R/'preserved-before.json').write_text(json.dumps({str(f.relative_to(ROOT)):hashlib.sha256(f.read_bytes()).hexdigest() for f in files},indent=2))
INPUT=ROOT/'artifacts/gpu-launch/20261031T053500Z/runtime/Release/inputs/linear';PROBLEM=ROOT/'artifacts/gpu-launch/20261031T053500Z/runtime/Release/inputs/nonlinear'
def invoke(cmd,dest,timeout=600,cwd=ROOT):
 dest.mkdir(parents=True,exist_ok=True)
 with (dest/'run.log').open('w') as f:result=subprocess.run(list(map(str,cmd)),stdout=f,stderr=subprocess.STDOUT,cwd=cwd,timeout=timeout)
 (dest/'invocation.json').write_text(json.dumps({'command':list(map(str,cmd)),'exit':result.returncode,'cwd':str(cwd)},indent=2));print(dest.name,result.returncode,flush=True)
 if result.returncode:raise RuntimeError((dest/'run.log').read_text(errors='replace')[-2000:])
def build(cfg):
 invoke(['cmake','--build',ROOT/'build/gpu-memory','--config',cfg,'--target','G64MemoryCandidate','--','/m:1','/v:m'],R/('build-'+cfg))
 source=ROOT/'build/gpu-memory/bin'/cfg;dest=R/'runtime'/cfg;dest.mkdir(parents=True,exist_ok=True);shutil.copy2(source/'G64MemoryCandidate.exe',dest);shutil.copytree(source/'shaders',dest/'shaders',dirs_exist_ok=True)
 for name in ['COPYING','LICENSE','README.HairLab.md']:shutil.copy2(ROOT/'third_party/qd-2.3.24'/name,dest/('QD-'+name))
 (R/('executed-'+cfg+'-hashes.json')).write_text(json.dumps({str(f.relative_to(dest)):hashlib.sha256(f.read_bytes()).hexdigest() for f in dest.rglob('*') if f.is_file()},indent=2));return dest/'G64MemoryCandidate.exe'
def run(exe,mode,inputs,out,layout='baseline',corrections=1):
 hashes=json.loads((R/('executed-'+exe.parent.name+'-hashes.json')).read_text())
 for name,h in hashes.items():assert hashlib.sha256((exe.parent/name).read_bytes()).hexdigest()==h
 invoke([exe,mode,inputs,out,layout,corrections],out,cwd=exe.parent)
 if mode in ['replay','formation']:
  status=json.loads((out/'STATUS.json').read_text());assert all(b['pass']==status['cases'] for b in status['budgets'] if b['corrections'])
if a.phase=='preflight':
 exe=build('Release');run(exe,'codec',INPUT,R/'codec')
 small=R/'small-inputs';small.mkdir(exist_ok=True)
 for name in ['primary__original_initial.json','heldout-slot0__original_initial.json','synthetic-2-pins0__original_initial.json','synthetic-2-pins1__original_initial.json','synthetic-2-pins2__original_initial.json','synthetic-2-pins3__original_initial.json']:shutil.copy2(INPUT/name,small)
 for layout in ['work32','workinput32']:
  run(exe,'replay',small,R/'focused'/layout,layout)
  run(exe,'ownership',INPUT,R/'ownership'/layout,layout)
  run(exe,'replay',INPUT,R/'Release'/layout/'replay',layout)
elif a.phase=='benchmark':
 exe=R/'runtime/Release/G64MemoryCandidate.exe';run(exe,'benchmark',INPUT,R/'study')
else:
 selection=json.loads((R/'study/SELECTION.json').read_text());layout=['baseline','work32','workinput32'][selection['accuracyCandidate']]
 for cfg in ['Release','Debug']:
  exe=build(cfg);dest=exe.parent/'inputs';shutil.copytree(INPUT,dest/'linear',dirs_exist_ok=True);shutil.copytree(PROBLEM,dest/'nonlinear',dirs_exist_ok=True)
  for mode in ['replay','formation']:run(exe,mode,dest/'linear',R/cfg/layout/mode,layout)
  if cfg=='Release':
   other='workinput32' if layout=='work32' else 'work32'
   run(exe,'formation',dest/'linear',R/cfg/other/'formation',other)
  run(exe,'hybrid1',dest/'nonlinear',R/cfg/layout/'hybrid1',layout)
  run(exe,'dynamics',dest/'nonlinear',R/cfg/layout/'dynamics16-c1',layout,1)
  if cfg=='Debug':run(exe,'ownership',dest/'linear',R/'ownership-Debug'/layout,layout)
