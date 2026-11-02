"""Export only locomotion joint metadata; preserve model, poses and hair."""
import json,struct,hashlib
from pathlib import Path
R=Path(__file__).resolve().parents[1];A=R/'assets/moca'
g=json.loads((A/'rig.json').read_text(encoding='utf-8'));names={b['name']:b['index'] for b in g['bones']}
required=['Hips','Upper_leg.L','Lower_leg.L','Foot.L','Upper_leg.R','Lower_leg.R','Foot.R','Upper_arm.L','Upper_arm.R']
# Original author naming may use Leg instead of Upper_leg.
for n in required:
    if n not in names:raise RuntimeError('Missing locomotion bone '+n+'; available '+str(list(names)))
b=(A/'character.bin').read_bytes();nv=struct.unpack_from('<I',b,4)[0]
floor=min(struct.unpack_from('<f',b,8+i*12+4)[0] for i in range(nv))
p=A/'walk-rig.bin';p.write_bytes(struct.pack('<4sIIf9I',b'WALK',1,len(g['bones']),floor,*(names[n] for n in required)))
report={'version':1,'joints':dict(zip(required,[names[n] for n in required])),'floorBindY':floor,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()}
(A/'walk-rig.json').write_text(json.dumps(report,indent=2))
m=json.loads((A/'manifest.json').read_text(encoding='utf-8'))
for n in ('walk-rig.bin','walk-rig.json'):m['files'][n]=hashlib.sha256((A/n).read_bytes()).hexdigest()
(A/'manifest.json').write_text(json.dumps(m,ensure_ascii=False,indent=2),encoding='utf-8');print(json.dumps(report))
