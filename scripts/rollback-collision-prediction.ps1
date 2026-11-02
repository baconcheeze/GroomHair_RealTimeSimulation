$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
Copy-Item -LiteralPath (Join-Path $projectRoot 'artifacts/collision-no-prediction/before/HairUpdateVelocity.h') -Destination (Join-Path $projectRoot 'third_party/JoltPhysics/Jolt/Shaders/HairUpdateVelocity.h') -Force
Copy-Item -LiteralPath (Join-Path $projectRoot 'artifacts/collision-no-prediction/before/Hair.cpp') -Destination (Join-Path $projectRoot 'third_party/JoltPhysics/Jolt/Physics/Hair/Hair.cpp') -Force
Write-Output 'Plane prediction and its constant-buffer bindings restored. Rebuild the app before running.'
