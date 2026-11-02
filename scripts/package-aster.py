"""Stage only the current Aster application and its runtime dependencies."""
import hashlib
import json
import shutil
from pathlib import Path

root = Path(__file__).resolve().parents[1]
source = root / "build/jolt-only/bin/Release"
dest = root / "artifacts/aster/runtime"
dest.mkdir(parents=True, exist_ok=True)
for name in ["HairLabDX12.exe", "dxcompiler.dll", "dxil.dll", "LICENSE", "IMGUI-LICENSE.txt"]:
    shutil.copy2(source/name, dest/name)
for folder in ["JoltShaders", "shaders", "Assets/Aster"]:
    (dest/folder).mkdir(parents=True, exist_ok=True)
for file in (source/"JoltShaders").glob("Hair*.dxil"):
    shutil.copy2(file, dest/"JoltShaders"/file.name)
shutil.copy2(root/"shaders/JoltDisplay.hlsl",dest/"shaders/JoltDisplay.hlsl")
shutil.copytree(root/"assets/aster",dest/"Assets/Aster",dirs_exist_ok=True)
for name in ["README.md", "ASTER_SCENE.md", "JOLT_ASSETS.md"]:
    if (root/name).exists():
        shutil.copy2(root/name,dest/name)
manifest = {str(p.relative_to(dest)):hashlib.sha256(p.read_bytes()).hexdigest()
            for p in dest.rglob("*") if p.is_file()}
(dest.parent/"runtime-sha256.json").write_text(json.dumps(manifest,indent=2))
print(dest/"HairLabDX12.exe")
