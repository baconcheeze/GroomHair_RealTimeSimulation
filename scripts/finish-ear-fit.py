"""Record source preservation and encode actual half-second app checkpoints."""
import json,hashlib,subprocess
from pathlib import Path
R=Path(__file__).resolve().parents[1]
old=json.loads((R/'artifacts/metahuman-silhouette/final/validation.json').read_text())
unchanged={n:hashlib.sha256((R/'assets/moca'/n).read_bytes()).hexdigest()==h for n,h in old['stagedSha256'].items() if n not in ('metahuman.hair','metahuman.materials')}
source=all(hashlib.sha256((R/'assets/MetaHuman'/n).read_bytes()).hexdigest()==h for n,h in old['sourceSha256'].items())
assert all(unchanged.values()) and source
O=R/'artifacts/metahuman-ear-fit'
(O/'preservation.json').write_text(json.dumps({'otherNativeAssetsUnchanged':unchanged,'allOriginalMetaHumanFilesUnchanged':source},indent=2))
files=sorted((O/'final/Release/side').glob('step-*.png'));lines=[]
for f in files:lines.extend(["file '"+f.as_posix()+"'",'duration 0.5'])
lines.append("file '"+files[-1].as_posix()+"'")
(O/'sequence.txt').write_text('\n'.join(lines))
subprocess.run(['ffmpeg','-hide_banner','-loglevel','error','-y','-f','concat','-safe','0','-i',str(O/'sequence.txt'),'-filter_complex','[0:v]fps=2,scale=800:-1:flags=lanczos,split[a][b];[a]palettegen[p];[b][p]paletteuse','-loop','0',str(O/'ear-fit-motion.gif')],check=True,timeout=30)
print('PASS source/assets preserved; GIF uses actual 30-step / 0.5s checkpoints, final frame held.')
