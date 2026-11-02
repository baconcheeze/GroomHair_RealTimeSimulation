"""GIF from actual bob app captures; 15 steps/60 Hz = 0.25 s per checkpoint."""
from pathlib import Path
import subprocess,shutil
r=Path(__file__).resolve().parents[1]
import json
groom=json.loads((r/'assets/moca/bob.json').read_text(encoding='utf-8'));refit='scalpSha256' in groom
out=r/('artifacts/moca-bob-volume' if groom.get('revision')=='flow-v3-volume' else ('artifacts/moca-bob-redesign' if groom.get('revision')=='flow-v2' else ('artifacts/moca-bob-refit' if refit else 'artifacts/moca-bob')));p=out/('validated/Release/bob' if groom.get('revision')=='flow-v2' else 'final/Release/bob')
frames=sorted(p.glob('step-*.png'));assert len(frames)==9
(p/'animation.txt').write_text(''.join("file '"+f.name+"'\nduration 0.25\n" for f in frames))
encoder=shutil.which('ffmpeg');assert encoder,'Numbered PNGs available; ffmpeg missing'
subprocess.run([encoder,'-y','-loglevel','error','-f','concat','-safe','0','-i',str(p/'animation.txt'),'-vf','fps=12,scale=640:-1:flags=lanczos,split[a][b];[a]palettegen[p];[b][p]paletteuse',str(out/'bob-motion.gif')],check=True)
