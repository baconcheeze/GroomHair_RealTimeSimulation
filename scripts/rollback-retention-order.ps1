$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
foreach ($name in @('HairUpdateVelocity.hlsl', 'HairUpdateVelocityIntegrate.hlsl')) {
    $source = Join-Path $projectRoot "artifacts/retention-after-collision/before/$name"
    $destination = Join-Path $projectRoot "third_party/JoltPhysics/Jolt/Shaders/$name"
    Copy-Item -LiteralPath $source -Destination $destination -Force
}
Write-Output 'Original retention-before-collision shaders restored. Rebuild HairLabDX12 to deploy.'
