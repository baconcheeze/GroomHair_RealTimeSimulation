"""Collect final integration evidence and encode actual app checkpoints."""
import hashlib,json,subprocess
from pathlib import Path
R=Path(__file__).resolve().parents[1];O=R/'artifacts/metahuman-finish'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
validation=json.loads((O/'verified/validation.json').read_text())
assert len(validation['runs'])==8 and all(x['exit']==0 for x in validation['runs'])
for n,h in validation['stagedSha256'].items():assert sha(R/'assets/moca'/n)==h,n
ear_reports=[json.loads(p.read_text()) for p in (O/'verified/clearance.json',O/'verified-close/clearance.json')]
for report in ear_reports:
    assert report['inputSha256']==sha(R/'assets/moca/metahuman.hair')
    for state in report['results']:
        for region in state['regions'].values():
            assert region['rootsInside']==0 and region['freeInside']==0 and region['crossingEdges']==0,(state['capture'],region)
mesh=json.loads((O/'verified/Release/walk/mesh-crossings.json').read_text())
assert len(mesh['results'])==7 and all(s['crossings']==0 for s in mesh['results'])
old=json.loads((R/'artifacts/metahuman-ear-fit/final/validation.json').read_text())
preserved={n:sha(R/'assets/moca'/n)==h for n,h in old['stagedSha256'].items() if n not in ('metahuman.hair','metahuman.materials','metahuman.ears')}
original={n:sha(R/'assets/MetaHuman'/n)==h for n,h in old['sourceSha256'].items()}
assert all(preserved.values()) and all(original.values())
folders=[R/'build/jolt-only/bin/Release',R/'build/jolt-only/bin/Debug',R/'build/vs2022/bin/Release',R/'build/vs2022/bin/Debug',R/'artifacts/moca/runtime']
for folder in folders:
    for n in ('metahuman.hair','metahuman.materials','metahuman.ears','metahuman.json'):
        assert sha(folder/'Assets/Moca'/n)==sha(R/'assets/moca'/n),(folder,n)
files=sorted((O/'verified-close/ear-right').glob('step-*.png'));lines=[]
for f in files:lines.extend(["file '"+f.as_posix()+"'",'duration 1.0'])
lines.append("file '"+files[-1].as_posix()+"'");(O/'motion-frames.txt').write_text('\n'.join(lines))
subprocess.run(['ffmpeg','-hide_banner','-loglevel','error','-y','-f','concat','-safe','0','-i',str(O/'motion-frames.txt'),'-filter_complex','[0:v]fps=1,scale=800:-1:flags=lanczos,split[a][b];[a]palettegen[p];[b][p]paletteuse','-loop','0',str(O/'side-finished.gif')],check=True,timeout=30)
subprocess.run(['ffmpeg','-hide_banner','-loglevel','error','-y','-i',str(O/'baseline/ear-right/step-0000.png'),'-i',str(O/'verified-close/ear-right/step-0000.png'),'-filter_complex','[0:v]scale=640:-1[a];[1:v]scale=640:-1[b];[a][b]hstack','-frames:v','1',str(O/'before-after.png')],check=True,timeout=30)
sources=['src/JoltMain.cpp','src/JoltScene.cpp','src/MetaHumanAttachment.h','scripts/import-metahuman.py','scripts/ear_surface.py','scripts/side_hair_fit.py','scripts/validate-metahuman.py','scripts/check-side-mesh.py','scripts/verify-side-finish.py']
summary={'profile':validation['profile'],'guides':validation['guideCount'],'render':validation['retainedStrands'],'removed':validation['removedStrands'],
         'verifiedRuns':len(validation['runs']),'earCaptureStates':sum(len(x['results']) for x in ear_reports),'earPointAndEdgeIntersections':0,
         'actualGpuSkinnedHeadTriangleCheckedSegments':sum(s['checkedSegments'] for s in mesh['results']),'actualHeadTriangleCrossings':sum(s['crossings'] for s in mesh['results']),
         'maxRootErrorMetres':max(x['maxRootErrorMetres'] for x in validation['runs']),
         'headLocalTipRmsDeformationMetres':validation['headLocalTipRmsDeformationMetres'],
         'otherNativeAssetsUnchanged':preserved,'originalPurchasedFilesAndPhotosUnchanged':original,
         'executableSha256':{str(f.relative_to(R)):sha(f/'HairLabDX12.exe') for f in folders},
         'sourceSha256':{n:sha(R/n) for n in sources},'nativeAssetSha256':{n:sha(R/'assets/moca'/n) for n in ('metahuman.hair','metahuman.materials','metahuman.ears','metahuman.json')},
         'capturePlayback':'Actual completed 0/60/120/180 step captures, 60Hz simulation, one second per image and final hold. No fabricated/interpolated motion.',
         'qualification':'Bounded app/controller checks and inspected images. No physical mouse/keyboard/resize claim; no untested pose/continuous-time guarantee; not a GPU benchmark.'}
(O/'summary.json').write_text(json.dumps(summary,indent=2))
print('PASS final assets/deployments, 8 integrations, ear points/edges, actual GPU-skinned head triangles, original preservation and actual-motion GIF')
