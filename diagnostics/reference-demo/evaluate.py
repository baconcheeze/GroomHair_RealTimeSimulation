"""Offline evaluation/animation encoding; never called by the native solver."""
from pathlib import Path
import json,math,subprocess,shutil,sys
R=Path(sys.argv[1]).resolve();ROOT=Path(__file__).resolve().parents[2];runs=[];encoder=shutil.which('ffmpeg')
for name in ['rest-3','tip-3','guides-32','guides-128']:
 d=R/'delivered'/name;frames=sorted(d.glob('step-*.png'));states=[json.loads(p.with_suffix('.json').read_text()) for p in frames];a,z=states[0],states[-1];reset=json.loads((d/'reset.json').read_text());assert len(set(t['guideIndex'] for t in z['strands']))==z['active'];assert z['active'] in [3,32,128]
 for s in states:
  assert s['failed']==0 and s['pinError']==0 and s['active']==z['active']
  for item,initial in zip(s['strands'],a['strands']):
   assert item['x'][0]==initial['x'][0] and item['q'][0]==initial['q'][0] and item['v'][0]==[0,0,0]
   assert all(math.isfinite(v) for key in ['x','q','v'] for vec in item[key] for v in vec)
 assert a['strands']==reset['strands'] and reset['step']==0 and reset['kicks']==0
 assert frames[0].read_bytes()==(d/'reset.png').read_bytes()
 assert z['maxDisplacement']>0 and frames[0].read_bytes()!=frames[-1].read_bytes()
 if encoder:
  listing=d/'animation-inputs.txt';listing.write_text(''.join("file '"+p.resolve().as_posix()+"'\nduration 0.2\n" for p in frames));cmd=[encoder,'-hide_banner','-loglevel','error','-y','-safe','0','-f','concat','-i',str(listing),'-filter_complex','split[a][b];[a]palettegen[p];[b][p]paletteuse','-fps_mode','vfr','-loop','0',str(d/'actual-motion.gif')];p=subprocess.run(cmd,stdout=(d/'animation.log').open('w'),stderr=subprocess.STDOUT,timeout=60);assert p.returncode==0
 runs.append({'name':name,'guides':z['active'],'steps':z['step'],'simulatedSeconds':z['simulatedSeconds'],'CPUWorkSeconds':z['CPUworkSeconds'],'checkpointBatchMs':[s['lastBatchMs'] for s in states[1:]],'tipMeters':z['maxTip'],'resetStateAndPNGExact':True,'finiteAndPins':True,'PNGFrames':len(frames),'GIFPlayback':'200ms per4physical substeps; 6x inspection slowdown, no interpolation'})
errors={k:0 for k in ['x','q','v']}
for p in (R/'delivered/rest-3').glob('step-*.json'):
 state=json.loads(p.read_text())
 for item,name in zip(state['strands'],['primary','heldout-slot0','heldout-slot2']):
  ref=json.loads((ROOT/'artifacts/native-ls/20261031T022012Z/runtime/isolated-dynamics'/f'{name}-16.json').read_text())['points'][state['step']]
  for k in errors:errors[k]=max(errors[k],max(math.dist(a,b) for a,b in zip(item[k],ref[k])))
assert errors['x']<1e-12 and errors['q']<1e-10 and errors['v']<1e-10
(R/'DELIVERY_VALIDATION.json').write_text(json.dumps({'runs':runs,'native16MaxErrors':errors,'encoder':encoder,'mouseInputTested':False},indent=2));print(json.dumps({'runs':runs,'native16MaxErrors':errors},indent=2))
