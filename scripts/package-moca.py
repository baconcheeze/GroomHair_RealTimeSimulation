"""Private local runtime package and executable-relative smoke test."""
import hashlib,json,shutil,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];SRC=ROOT/'build/jolt-only/bin/Release';DEST=ROOT/'artifacts/moca/runtime'
DEST.mkdir(parents=True,exist_ok=True)
for n in ('HairLabDX12.exe','dxcompiler.dll','dxil.dll','LICENSE','IMGUI-LICENSE.txt'):
    shutil.copy2(SRC/n,DEST/n)
for n in ('JoltShaders','shaders','Assets/Moca'):
    shutil.copytree(SRC/n,DEST/n,dirs_exist_ok=True)
for n in ('README.md','GUIDE_DENSITY.md','SHOULDER_FIT.md','GPU_BODY_PRIMITIVES.md','WAVE_HIP_HOP.md','BODY_COLLIDERS.md','COLLISION_PROFILE.md','AIR_RESISTANCE.md','SIDE_BANGS_V15.md','SIDEBURN_V14.md','METAHUMAN3_MATCH.md','EAR_FILL.md','EAR_ONLY_RESTORE.md','SIDE_HAIR_REDESIGN.md','EAR_WRAP.md','NAPE_COVERAGE.md','HAIR_SALON_FINISH.md','SIDE_HAIR_FINISH.md','NARUTO_RUN.md','WALKING_DEMO.md','DENIM_JACKET.md','CLOTHING_FIX.md','LAYERED_BOB.md','SALON_STYLE.md','UNIFORM_HAIR.md','EDITORIAL_STYLE.md','JOLT_ASSETS.md','MOCA_SCENE.md','BOB_SCENE.md','BOB_REDESIGN.md','BOB_VOLUME.md','FRINGE_MOTION_DIAGNOSIS.md','SCALP_REINFORCEMENT.md'):
    shutil.copy2(ROOT/n,DEST/n)
out=ROOT/'artifacts/moca/packaged-test';out.mkdir(parents=True,exist_ok=True)
command=[str(DEST/'HairLabDX12.exe'),'--steps','30','--capture-every','15','--reset-check','--capture-dir',str(out),'--log',str(out/'app.log')]
# An unrelated directory verifies executable-relative shaders and assets.
p=subprocess.Popen(command,cwd=ROOT.parent)
try:code=p.wait(timeout=90)
except subprocess.TimeoutExpired:
    p.terminate();p.wait(timeout=10);raise RuntimeError('Packaged application timed out')
assert code==0,code
files={str(p.relative_to(DEST)):hashlib.sha256(p.read_bytes()).hexdigest() for p in DEST.rglob('*') if p.is_file() and p.name!='package.json'}
(DEST/'package.json').write_text(json.dumps({'privateLocalPackage':True,'testExit':code,'testCwd':str(ROOT.parent),'command':command,'files':files},indent=2))
print(DEST/'HairLabDX12.exe')
