$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
Push-Location $root
try {
    cmake --preset vs2022
    if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed' }
    Invoke-Item -LiteralPath (Join-Path $root 'build/jolt-only/HairLabDX12.sln')
} finally { Pop-Location }
