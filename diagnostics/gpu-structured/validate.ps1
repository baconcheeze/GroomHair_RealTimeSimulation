param([ValidateSet('Debug','Release')][string]$Configuration='Release',[string]$RunRoot)
$ErrorActionPreference='Stop'
$project=(Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
if(-not $RunRoot){$RunRoot=(Get-Content (Join-Path $project 'build/gpu-structured-run.txt')).Trim()}
if(-not [IO.Path]::IsPathRooted($RunRoot)){$RunRoot=Join-Path $project $RunRoot}
$bin=Join-Path $project "build/gpu-structured/bin/$Configuration"
$target=Join-Path $RunRoot "final-$Configuration"
New-Item -ItemType Directory -Force $target | Out-Null
$inputs=Join-Path $project 'artifacts/ls-refinement/20261030T152921Z/suite-inputs'
$small=Join-Path $project 'artifacts/qr-nullspace/20261030T145943Z/frozen'
$problems=Join-Path $project 'artifacts/strand-block/20261030T141357Z/inputs'
$records=@()
function Invoke-Test([string]$Name,[string]$Executable,[string[]]$Arguments,[int[]]$Accepted=@(0)){
 & $Executable @Arguments *> (Join-Path $target "$Name.log")
 $exitCode=$LASTEXITCODE
 if($Accepted -notcontains $exitCode){throw "$Name failed with exit $exitCode; inspect $target/$Name.log"}
 $script:records+=@{test=$Name;exit=$exitCode}
 Write-Output "$Configuration $Name exit=$exitCode"
}
Invoke-Test 'probe' "$bin/GpuStructuredProbe.exe" @($inputs,"$target/probe")
Invoke-Test 'small' "$bin/GpuStructuredLSCandidate.exe" @('replay',$small,"$target/small")
Invoke-Test 'g64' "$bin/GpuStructuredLSCandidate.exe" @('replay',$inputs,"$target/g64")
Invoke-Test 'formation' "$bin/GpuStructuredLSCandidate.exe" @('formation',$inputs,"$target/formation")
Invoke-Test 'mixed' "$bin/GpuStructuredLSCandidate.exe" @('mixed',$inputs,"$target/mixed") @(0,1)
Invoke-Test 'hybrid5' "$bin/GpuStructuredLSCandidate.exe" @('hybrid5',$problems,"$target/hybrid5")
Invoke-Test 'hybrid1' "$bin/GpuStructuredLSCandidate.exe" @('hybrid1',$problems,"$target/hybrid1")
Invoke-Test 'dynamics16' "$bin/GpuStructuredLSCandidate.exe" @('dynamics',$problems,"$target/dynamics16")
if($Configuration -eq 'Release'){Invoke-Test 'benchmark' "$bin/GpuStructuredLSCandidate.exe" @('benchmark',$inputs,"$target/benchmark")}
$records | ConvertTo-Json -Depth 5 | Set-Content -Encoding UTF8 (Join-Path $target 'execution.json')
Get-ChildItem $bin -Recurse -File | Where-Object {$_.Extension -eq '.dxil' -or $_.Name -like 'GpuStructured*.exe'} | ForEach-Object {@{path=$_.FullName;SHA256=(Get-FileHash -Algorithm SHA256 -LiteralPath $_.FullName).Hash}} | ConvertTo-Json -Depth 4 | Set-Content -Encoding UTF8 (Join-Path $target 'executed-binary-hashes.json')
