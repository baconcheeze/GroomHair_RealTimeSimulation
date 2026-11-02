"""Hide inner bra and animal tail, without deleting geometry or rig.
Sets a display-only Surface.flags bit; original VRM and Jolt mesh stay intact.
Run after import/scalp preparation. Idempotent; no mesh offsets are hard-coded.
"""
import hashlib,json,struct,shutil
from pathlib import Path
R=Path(__file__).resolve().parents[1];A=R/'assets/moca';O=R/'artifacts/appearance'
O.mkdir(parents=True,exist_ok=True)
raw=next((R/'assets/imported/moca').glob('*.vrm')).read_bytes()
size=struct.unpack_from('<I',raw,12)[0];g=json.loads(raw[20:20+size]);data=raw[28+size:]
surface=bytearray((A/'surface.bin').read_bytes());mesh=(A/'character.bin').read_bytes()
nv=struct.unpack_from('<I',surface)[0];assert len(surface)==4+nv*64
assert struct.unpack_from('<I',mesh,4)[0]==nv
base=0;hidden=[]
for node in g['nodes']:
    if 'mesh' not in node or node.get('name')=='Hair_Kemomimi':continue
    for primitive in g['meshes'][node['mesh']]['primitives']:
        if primitive['material']==1:continue
        accessor=g['accessors'][primitive['attributes']['POSITION']]
        count=accessor['count']
        if node.get('name') in ('Underwear_Bra','Hair_Tail'):
            view=g['bufferViews'][accessor['bufferView']]
            offset=view.get('byteOffset',0)+accessor.get('byteOffset',0)
            assert accessor['componentType']==5126 and accessor['type']=='VEC3'
            for i in range(count):
                vertex=base+i;at=4+vertex*64
                assert data[offset+i*view.get('byteStride',12):offset+i*view.get('byteStride',12)+12]==mesh[8+vertex*12:20+vertex*12]
                assert struct.unpack_from('<I',surface,at+8)[0]==(3 if node['name']=='Hair_Tail' else 4)
                flags=struct.unpack_from('<I',surface,at+12)[0]
                struct.pack_into('<I',surface,at+12,flags|1)
            hidden.append({'node':node['name'],'firstVertex':base,'vertices':count,
                           'triangles':g['accessors'][primitive['indices']]['count']//3})
        base+=count
assert {h['node'] for h in hidden}=={'Underwear_Bra','Hair_Tail'} and base<=nv
backup=O/'before/surface.bin';backup.parent.mkdir(parents=True,exist_ok=True)
if not backup.exists():shutil.copy2(A/'surface.bin',backup)
(A/'surface.bin').write_bytes(surface)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
report={'displayOnly':True,'flagBit':1,'hidden':hidden,'reason':'Hide intersecting inner bra and user-requested animal tail; preserve original rig',
        'sourceVrmSha256':hashlib.sha256(raw).hexdigest(),'characterSha256':sha(A/'character.bin'),
        'hairSha256':sha(A/'bob.hair'),'surfaceSha256':sha(A/'surface.bin')}
(A/'outfit.json').write_text(json.dumps(report,indent=2))
m=json.loads((A/'manifest.json').read_text(encoding='utf-8'))
for name in ('surface.bin','outfit.json'):m['files'][name]=sha(A/name)
m['outfit']='Halter top; inner Underwear_Bra and Hair_Tail hidden by display-only surface flag 1'
(A/'manifest.json').write_text(json.dumps(m,ensure_ascii=False,indent=2),encoding='utf-8')
(O/'outfit-validation.json').write_text(json.dumps(report,indent=2));print(json.dumps(report,indent=2))
