param([ValidateSet('Debug','Release')][string]$Configuration='Release')
$ErrorActionPreference='Stop'
Push-Location (Split-Path $PSScriptRoot -Parent)
try {
    cmake --preset vs2022
    if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed' }
    cmake --build build/jolt-only --config $Configuration --target HairLabDX12 -- /m:1
    if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
} finally { Pop-Location }
