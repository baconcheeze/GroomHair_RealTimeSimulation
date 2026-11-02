param(
    [ValidateSet('Debug','Release')][string]$Configuration='Release',
    [ValidateRange(0,10000)][int]$Steps=60,
    [switch]$Interactive
)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
$exe=Join-Path $projectRoot "build/vs2022/bin/$Configuration/HairLabDX12.exe"
if (!(Test-Path -LiteralPath $exe)) { throw 'Build HairLabDX12 first.' }
$runId=(Get-Content -LiteralPath (Join-Path $projectRoot 'build/autonomous-run.txt')).Trim()
$stamp=[DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ')
$output=Join-Path $projectRoot "artifacts/autonomous/$runId/manual/$stamp"
New-Item -ItemType Directory -Path $output -Force | Out-Null
$arguments=@('--scene','hair','--draw','guides','--view','front','--solver-mode','dynamics','--participating','all','--sim-initializer','guarded','--sim-h','0.008333333333333333','--sweeps-per-step','32','--log',"$output/run.log")
Write-Host 'Research candidate: strain and trajectory accuracy gates remain unmet. See REPORT.md.'
if (!$Interactive) { $arguments+=@('--sim-steps',"$Steps",'--frames','1','--capture',"$output/guides.png",'--report',"$output/report.json") }
& $exe @arguments
if ($LASTEXITCODE -ne 0) { throw "HairLabDX12 failed: $LASTEXITCODE" }
Write-Host "Outputs: $output"
