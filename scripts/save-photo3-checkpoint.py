"""Immutable, independently runnable approved checkpoint (project has no Git)."""
from pathlib import Path
import json,hashlib,shutil,subprocess,datetime
R=Path(__file__).resolve().parents[1];C=R/'checkpoints/photo3-v13-approved'
assert not C.exists(),'Approved checkpoint already exists; never overwrite it.'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
runtime=R/'artifacts/moca/runtime'
for n in ('metahuman.hair','metahuman.materials','metahuman.ears','metahuman.skull','metahuman.json'):
 assert sha(runtime/'Assets/Moca'/n)==sha(R/'assets/moca'/n),n
assert sha(runtime/'shaders/JoltDisplay.hlsl')==sha(R/'shaders/JoltDisplay.hlsl')
shutil.copytree(runtime,C/'runnable')
sources=['CMakeLists.txt','CMakePresets.json','README.md','METAHUMAN3_MATCH.md']
sources+=['src/'+n for n in ('JoltMain.cpp','JoltScene.cpp','JoltScene.h','CharacterLocomotion.cpp','CharacterLocomotion.h','CharacterTexture.cpp','CharacterTexture.h','SceneCapture.cpp','SceneCapture.h','DenimJacket.cpp','DenimJacket.h','OrbitCamera.h','MetaHumanAttachment.h')]
sources+=['shaders/JoltDisplay.hlsl']
sources+=['scripts/'+n for n in ('match-metahuman3.py','ear_surface.py','groom_surface.py','groom_drape.py','check-side-redesign.py','validate-metahuman.py','package-moca.py')]
sources+=[str(p.relative_to(R)).replace('\\','/') for p in (R/'authoring/side-style').iterdir() if p.is_file()]
for name in sources:
 d=C/'source'/name;d.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(R/name,d)
E=C/'evidence';E.mkdir()
shutil.copy2(R/'artifacts/metahuman3-match/reference3-versus-app.png',E/'reference3-versus-app.png')
shutil.copy2(R/'artifacts/metahuman3-match/accepted-summary.json',E/'accepted-summary.json')
command=[str(C/'runnable/HairLabDX12.exe'),'--view','audit-right','--hide-body','--frames','1','--paused','--capture-dir',str(E/'smoke'),'--log',str(E/'smoke/app.log')]
p=subprocess.run(command,cwd=R.parent,timeout=90);assert p.returncode==0
s=json.loads((E/'smoke/step-0000.json').read_text());assert s['finite'] and s['render']==33512 and s['guides']==17329
restore=r'''param([switch]$IncludeSource)
$ErrorActionPreference='Stop'
$checkpointRoot=$PSScriptRoot
$projectRoot=Split-Path (Split-Path $checkpointRoot -Parent) -Parent
# Run with HairLabDX12 closed. Restore only this groom by default.
foreach($name in @('metahuman.hair','metahuman.materials','metahuman.ears','metahuman.skull','metahuman.json','manifest.json')) {
 Copy-Item -LiteralPath (Join-Path $checkpointRoot "runnable/Assets/Moca/$name") -Destination (Join-Path $projectRoot "assets/moca/$name")
 foreach($build in @('jolt-only','vs2022')) { foreach($config in @('Debug','Release')) {
  $dest=Join-Path $projectRoot "build/$build/bin/$config/Assets/Moca/$name"
  if(Test-Path (Split-Path $dest -Parent)){Copy-Item -LiteralPath (Join-Path $checkpointRoot "runnable/Assets/Moca/$name") -Destination $dest}
 }}
}
if($IncludeSource){
 $sourceRoot=Join-Path $checkpointRoot 'source'
 Get-ChildItem -LiteralPath $sourceRoot -Recurse -File | ForEach-Object {
  $relative=$_.FullName.Substring($sourceRoot.Length+1)
  $dest=Join-Path $projectRoot $relative
  New-Item -ItemType Directory -Force -Path (Split-Path $dest -Parent) | Out-Null
  Copy-Item -LiteralPath $_.FullName -Destination $dest
 }
}
Write-Output 'Approved v13 groom restored. Use runnable/HairLabDX12.exe for the exact saved binary.'
'''
(C/'restore.ps1').write_text(restore,encoding='utf-8-sig')
(C/'README.md').write_text('''# 승인된 메타휴먼3 v13 체크포인트

사용자가 "꽤 괜찮다"고 확인한 버전. Git 브랜치가 아닌 독립 실행 스냅샷이다. 원래 파일과 링크를 공유하지 않는 실제 복사본이므로 현재 작업 변경의 영향을 받지 않는다.

실행: `runnable\\HairLabDX12.exe --view bust-side --run`

복원: 앱을 닫고 `powershell -File checkpoints\\photo3-v13-approved\\restore.ps1`. 기본은 현재 프로젝트의 해당 헤어 데이터만 복원한다. `-IncludeSource`를 명시하면 저장한 관련 코드도 복원하며 현재 편집을 덮어쓴다. 정확한 기존 실행 파일은 언제든 runnable에서 직접 실행한다. 불변 Jolt/imgui 소스는 프로젝트의 기존 third_party를 사용하며, 코드 복원 후 재빌드가 필요하다.

원본 구매 에셋 라이선스 적용/비공개 로컬 보관. 걷기 일부 자세의 기존 관통 한계도 그대로 보존한다. Unity/원본 Alembic/Blender 없이 실행할 수 있다. manifest.json은 모든 저장 파일의 SHA-256을 기록한다.
''',encoding='utf-8')
manifest={'name':'photo3-v13-approved','kind':'independent runtime + focused source checkpoint; not Git branch','createdUtc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'smokeExit':0,'renderStrands':33512,'guides':17329,'files':{str(p.relative_to(C)).replace('\\','/'):sha(p) for p in C.rglob('*') if p.is_file()}}
(C/'manifest.json').write_text(json.dumps(manifest,indent=2))
print('Approved checkpoint saved and smoke-tested:',C)
