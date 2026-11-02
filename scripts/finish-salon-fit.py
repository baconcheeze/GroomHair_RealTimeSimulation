"""Collect final app evidence without changing geometry or old reports."""
from pathlib import Path
import hashlib,json,subprocess
R=Path(__file__).resolve().parents[1]
O=R/'artifacts/hair-salon-finish'
p=R/'README.md';s=p.read_text(encoding='utf-8')
notice='> 최신 마감: [HAIR_SALON_FINISH.md](HAIR_SALON_FINISH.md). 윗머리·귀 뒤·아래 시점의 연결을 다시 제작했습니다. 정지 및 헤드턴 검사와 별도로, 걷기 일부 자세의 미세 관통은 남아 있습니다. 이전 상세 설명보다 최신 문서를 우선합니다.\n\n'
if not s.startswith(notice):s=notice+s
s=s.replace('ear-finish-v6','salon-drape-v7').replace('22,549','15,871').replace('7개 물성','8개 물성')
p.write_text(s,encoding='utf-8')
p=R/'scripts/package-moca.py';s=p.read_text()
if "'HAIR_SALON_FINISH.md'" not in s:s=s.replace("('README.md','SIDE_HAIR_FINISH.md'","('README.md','HAIR_SALON_FINISH.md','SIDE_HAIR_FINISH.md'")
p.write_text(s)
paths=[(O/'verified-motion/below'/f'step-{i:04}.png').resolve().as_posix() for i in (0,60,120,180)]
f=O/'motion-frames.txt';f.write_text(''.join(f"file '{p}'\nduration 1\n" for p in paths)+f"file '{paths[-1]}'\n")
subprocess.run(['ffmpeg','-y','-f','concat','-safe','0','-i',str(f),'-vf','fps=8,scale=768:-1:flags=lanczos,split[s0][s1];[s0]palettegen[p];[s1][p]paletteuse','-loop','0',str(O/'head-turn.gif')],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
files=[R/'src/JoltMain.cpp',R/'src/JoltScene.cpp',R/'scripts/import-metahuman.py',R/'scripts/groom_drape.py']
files+=list((R/'assets/moca').glob('metahuman.*'))
files+=[R/f'build/{b}/bin/{c}/HairLabDX12.exe' for b in ('jolt-only','vs2022') for c in ('Debug','Release')]
(O/'final-hashes.json').write_text(json.dumps({str(p.relative_to(R)):hashlib.sha256(p.read_bytes()).hexdigest() for p in files},indent=2))
print('Final hashes and actual-capture GIF written; geometry unchanged.')
