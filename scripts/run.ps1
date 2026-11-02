param([ValidateSet('Debug','Release')][string]$Configuration='Release', [int]$Frames=0, [int]$Steps=0, [switch]$Paused)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$exe=Join-Path $root "build/jolt-only/bin/$Configuration/HairLabDX12.exe"
if (!(Test-Path $exe)) { throw 'Executable missing; run scripts/build.ps1 first.' }
$arguments=@('--log',"$root/artifacts/jolt-session.log")
if ($Frames -lt 0) { throw 'Frames must be >= 0' }
if ($Frames -gt 0) { $arguments+=@('--frames',"$Frames") }
if ($Steps -gt 0) { $arguments+=@('--steps',"$Steps") }
if ($Paused) { $arguments+='--paused' }
$process=Start-Process -FilePath $exe -ArgumentList $arguments -WindowStyle Normal -PassThru -Wait
if ($process.ExitCode -ne 0) { throw "HairLabDX12 failed: $($process.ExitCode)" }
