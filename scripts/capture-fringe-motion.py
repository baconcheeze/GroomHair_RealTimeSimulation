"""Encode actual completed app frames. No synthetic motion/interpolation."""
import shutil,subprocess
from pathlib import Path
R=Path(__file__).resolve().parents[1];O=R/'artifacts/fringe-diagnosis/final';P=O/'Release/moving'
frames=sorted(P.glob('step-*.png'));assert len(frames)==17
(P/'animation.txt').write_text(''.join("file '"+f.name+"'\nduration 0.25\n" for f in frames))
encoder=shutil.which('ffmpeg');assert encoder,'PNG sequence is available; ffmpeg not installed'
for name,prefix in [('fringe-motion.gif','scale=800:-1:flags=lanczos'),('fringe-motion-closeup.gif','crop=400:450:440:115,scale=640:-1:flags=lanczos')]:
    filt='fps=12,'+prefix+',split[a][b];[a]palettegen[p];[b][p]paletteuse'
    subprocess.run([encoder,'-y','-loglevel','error','-f','concat','-safe','0','-i',str(P/'animation.txt'),'-vf',filt,str(O/name)],check=True)
