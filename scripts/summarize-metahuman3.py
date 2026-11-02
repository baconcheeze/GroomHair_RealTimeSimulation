"""Record final fixed-input evidence; does not alter assets or expectations."""
from pathlib import Path
import json,hashlib,struct,subprocess
R=Path(__file__).resolve().parents[1];O=R/'artifacts/metahuman3-match'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
v=json.loads((O/'accepted-regression/validation.json').read_text())
views=json.loads((R/'artifacts/hair-side-redesign/photo3-accepted/runs.json').read_text())
assert len(v['runs'])==8 and len(views['runs'])==9
assert all(x['exit']==0 for x in v['runs']+views['runs'])
original=json.loads((R/'artifacts/hair-salon-finish/verified/validation.json').read_text())
assert v['sourceSha256']==original['sourceSha256']
for n in ('character.bin','bob.hair','studio.hair','naruto-run.bin'):assert v['stagedSha256'][n]==original['stagedSha256'][n]
base=(R/'authoring/side-style/metahuman.hair').read_bytes();current=(R/'assets/moca/metahuman.hair').read_bytes()
materials=[x[0] for x in struct.iter_unpack('<I',(R/'authoring/side-style/metahuman.materials').read_bytes()[4:])]
for i,k in enumerate(materials):
 if k in (1,3,7):assert base[128+i*384:128+(i+1)*384]==current[128+i*384:128+(i+1)*384]
mesh=json.loads((O/'accepted-regression/Release/walk/mesh-crossings.json').read_text())
ear=json.loads((R/'artifacts/hair-side-redesign/photo3-accepted/clearance.json').read_text())
paths=[R/'src/JoltMain.cpp',R/'src/JoltScene.cpp',R/'shaders/JoltDisplay.hlsl',R/'scripts/match-metahuman3.py',R/'scripts/ear_surface.py']
paths+=list((R/'assets/moca').glob('metahuman.*'))
for b in ('jolt-only','vs2022'):
 for c in ('Release','Debug'):
  folder=R/f'build/{b}/bin/{c}';paths.append(folder/'HairLabDX12.exe')
  assert sha(folder/'shaders/JoltDisplay.hlsl')==sha(R/'shaders/JoltDisplay.hlsl')
  for n in ('metahuman.hair','metahuman.materials','metahuman.ears','metahuman.skull','metahuman.json'):
   assert sha(folder/'Assets/Moca'/n)==sha(R/'assets/moca'/n)
report={'profile':v['profile'],'guideCount':v['guideCount'],'renderCount':v['retainedStrands'],'regressionRuns':8,'viewAndMotionRuns':9,
 'headLocalTipRmsDeformationMetres':v['headLocalTipRmsDeformationMetres'],'sourceAssetsUnchanged':True,'sourceGeometryRegionsBitwiseUnchanged':[1,3,7],
 'staticMesh':mesh['results'][0],'walkMeshCrossingsByStep':[{k:x[k] for k in ('capture','crossings','crossingsAwayFromFollicles')} for x in mesh['results']],
 'headTurnMaxEarInsidePoints':max(sum(a['freeInside'] for a in x['regions'].values()) for x in ear['results']),
 'headTurnMaxEarCrossingEdges':max(sum(a['crossingEdges'] for a in x['regions'].values()) for x in ear['results']),
 'hashes':{str(p.relative_to(R)):sha(p) for p in paths},
 'qualification':'Actual app captures and bounded controller tests. Static photo fit improved; not pixel-identical Unreal shading or all-pose no-intersection certification. Walking intersections remain explicitly reported.'}
(O/'accepted-summary.json').write_text(json.dumps(report,indent=2))
folder=R/'artifacts/hair-side-redesign/photo3-accepted/Release/audit-low-right-motion'
images=[(folder/f'step-{i:04}.png').resolve().as_posix() for i in (0,60,120)]
sequence=O/'motion-frames.txt';sequence.write_text(''.join(f"file '{p}'\nduration 1\n" for p in images)+f"file '{images[-1]}'\n")
subprocess.run(['ffmpeg','-y','-f','concat','-safe','0','-i',str(sequence),'-vf','fps=8,scale=768:-1:flags=lanczos,split[a][b];[a]palettegen[p];[b][p]paletteuse','-loop','0',str(O/'head-turn.gif')],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
print('Accepted evidence and actual 1Hz checkpoint GIF recorded.')
