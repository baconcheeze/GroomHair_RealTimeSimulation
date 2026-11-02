"""Encode actual 60-Hz app checkpoints: 15 steps = 0.25 simulated seconds."""
from pathlib import Path
import subprocess,shutil
r=Path(__file__).resolve().parents[1];p=r/'artifacts/moca/final/Release/motion'
frames=sorted(p.glob('step-*.png'));assert len(frames)==9
text=''.join("file '"+f.name+"'\nduration 0.25\n" for f in frames)
(p/'animation.txt').write_text(text)
encoder=shutil.which('ffmpeg')
if not encoder:raise RuntimeError('ffmpeg unavailable; numbered PNG sequence remains available')
subprocess.run([encoder,'-y','-loglevel','error','-f','concat','-safe','0','-i',str(p/'animation.txt'),'-vf','fps=12,scale=640:-1:flags=lanczos,split[a][b];[a]palettegen[p];[b][p]paletteuse',str(r/'artifacts/moca/moca-motion.gif')],check=True)
