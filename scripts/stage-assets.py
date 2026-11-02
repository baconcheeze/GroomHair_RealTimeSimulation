"""Read-only migration. All outputs are rooted in HairLabDX12/assets."""
import argparse, hashlib, json, pathlib, re, shutil, struct

root = pathlib.Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser()
p.add_argument('--source', type=pathlib.Path, required=True)
args = p.parse_args()
source = args.source.resolve()
out = root / 'assets'
records = []
def record(path, destination=None):
    raw = path.read_bytes()
    item = {'source': str(path), 'sourceSHA256': hashlib.sha256(raw).hexdigest()}
    if destination:
        target = out / destination
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(raw)
        item.update(staged=str(target.relative_to(root)), stagedSHA256=hashlib.sha256(raw).hexdigest())
    records.append(item)
    return raw

base = source / 'Assets/HairRest'
scene = record(base / 'Scenes/HairRestPreview.unity').decode('utf-8-sig')
def reference(field):
    guid = re.search(r'^  '+field+r': \{fileID: \d+, guid: ([0-9a-f]+),', scene, re.M)[1]
    matches = [m.with_suffix('') for m in base.rglob('*.meta') if re.search(r'^guid: '+guid+r'$', m.read_text(encoding='utf-8-sig'), re.M)]
    if len(matches) != 1: raise ValueError(f'Cannot resolve {field}: {guid}')
    return matches[0]
hair = reference('restSnapshot')
head = reference('replacementHeadMesh')
if not re.search(r'^  useReplacementHead: 1$', scene, re.M): raise ValueError('Selected head is no longer replacement: inspect migration')
if not re.search(r'm_LocalRotation: \{x: 0, y: 0, z: 0, w: 1\}\s+m_LocalPosition: \{x: 0, y: 0, z: 0\}\s+m_LocalScale: \{x: 1, y: 1, z: 1\}',scene): raise ValueError('Nonidentity scene pose requires review')
rawhair = record(hair, 'hair/SelectedRest.bytes')
magic, version, strands, vertices, guides = struct.unpack_from('<5I', rawhair)
assert magic == 0x5452484a and version == 1
assert len(rawhair) == 20 + 4*(strands+1) + 12*vertices + 4*guides
record(reference('headSnapshot'), 'head/JoltHeadRest.bytes')
text = record(head).decode('utf-8-sig')
nv = int(re.search(r'm_VertexCount: (\d+)',text)[1])
channels = [tuple(map(int,m)) for m in re.findall(r'- stream: (\d+)\s+offset: (\d+)\s+format: (\d+)\s+dimension: (\d+)',text)]
assert channels[0] == (0,0,0,3) and channels[1] == (0,12,0,3)
assert all(s==0 and f==0 for s,o,f,d in channels if d)
stride = max(o+d*4 for s,o,f,d in channels if d)
data = bytes.fromhex(re.search(r'_typelessdata: ([0-9a-f]+)',text)[1])
assert len(data) == int(re.search(r'm_DataSize: (\d+)',text)[1]) == nv*stride
assert re.search(r'm_StreamData:\s+serializedVersion: \d+\s+offset: 0\s+size: 0',text)
ib = bytes.fromhex(re.search(r'm_IndexBuffer: ([0-9a-f]+)',text)[1])
fmt = 'H' if int(re.search(r'm_IndexFormat: (\d+)',text)[1]) == 0 else 'I'
indices = struct.unpack('<'+fmt*(len(ib)//struct.calcsize(fmt)),ib)
assert len(indices)%3==0 and max(indices)<nv
parts = re.findall(r'firstByte: (\d+)\s+indexCount: (\d+)\s+topology: (\d+)\s+baseVertex: (\d+)',text)
assert parts and all(int(t)==0 and int(b)==0 for a,n,t,b in parts)
assert sum(int(n) for a,n,t,b in parts)==len(indices)
baked = bytearray(struct.pack('<4I',0x314d484e,1,nv,len(indices))) # NHM1
for i in range(nv): baked.extend(data[i*stride:i*stride+24])
baked.extend(struct.pack('<'+'I'*len(indices),*indices))
target=out/'head/SelectedHead.nhm'; target.parent.mkdir(parents=True,exist_ok=True); target.write_bytes(baked)
records[-1].update(staged=str(target.relative_to(root)),stagedSHA256=hashlib.sha256(baked).hexdigest(),conversion='Extract exact serialized Unity float3 position/normal and uint indices; no coordinate or winding change')
for name in ['Runtime/HairRestData.cs','Runtime/HairRestPreview.cs','Runtime/JoltHeadRestData.cs','Runtime/JoltHeadRestPreview.cs','Runtime/HairInspectionCamera.cs','Editor/FemaleHeadAuthoring.cs','Editor/FemaleHairFitting.cs']:
    record(base/name)
for path, dest in [(base/'FemaleHead/SOURCE.json','notices/FemaleHead-SOURCE.json'),(base/'FemaleHead/README.md','notices/FemaleHead-README.md'),(base/'Data/SOURCE.md','notices/JoltHair-SOURCE.md'),(source/'Tools/HairRest/Jolt-LICENSE.txt','notices/Jolt-LICENSE.txt')]: record(path,dest)
record(source/'Tools/HairRest/Validation/FemaleDensity/Unity-validation.log','reference/Unity-validation.log')
manifest={'version':1,'dataset':hair.stem,'units':'meters','space':'Unity left-handed neck-local; scene object identity', 'hairConversion':'JHRT stores Jolt neck-local, including fitted female data; reflect Z once on load', 'headConversion':'Selected Unity Mesh already reflected and winding reversed: extract exactly; original JHHD uses frame0 neck-local positions, reflect Z and reverse triangles once', 'pose':'Selected FemaleHead.asset baked authoring pose: scale(.85,.90,.90)+offset(0,-1.283,.015), inverse Jolt frame0 neck, H. Original scalp: exported frame0 skin pose, NOT bind pose. Camera basis inverse(H*frame0Neck*H). No object transform.', 'counts':{'renderStrands':strands,'renderVertices':vertices,'guideStrands':guides,'headVertices':nv,'headTriangles':len(indices)//3},'files':records}
(out/'manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
(out/'dataset.txt').write_text(hair.stem+'\n',encoding='utf-8')
print(json.dumps(manifest['counts']))
