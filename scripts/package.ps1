[CmdletBinding()]
param([string]$BuildDir,[string]$DistDir)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
if (-not $BuildDir) { $BuildDir = Join-Path $repoRoot 'build' }
if (-not $DistDir) { $DistDir = Join-Path $repoRoot 'dist' }
$version = (Get-Content -LiteralPath (Join-Path $repoRoot 'VERSION') -Raw).Trim()
if ($version -notmatch '^\d+\.\d+\.\d+(?:-[a-zA-Z0-9.]+)?$') { throw 'Invalid VERSION value' }
$exe = Join-Path $BuildDir 'G75Battery.exe'
if (-not (Test-Path -LiteralPath $exe)) { throw 'Build first with scripts/build.ps1.' }
$test = Start-Process -FilePath $exe -ArgumentList '--self-test' -WindowStyle Hidden -Wait -PassThru
if ($test.ExitCode -ne 0) { throw "Self-test failed: $($test.ExitCode)" }
$name = "MCHOSE-Keyboard-Battery-$version-windows-x64"
$stage = Join-Path $BuildDir ('package-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $stage,$DistDir | Out-Null
Copy-Item -LiteralPath $exe -Destination (Join-Path $stage 'G75Battery.exe')
Copy-Item -LiteralPath (Join-Path $repoRoot 'LICENSE'),(Join-Path $repoRoot 'README.md'),(Join-Path $repoRoot 'CHANGELOG.md') -Destination $stage
Copy-Item -LiteralPath (Join-Path $repoRoot 'docs') -Destination (Join-Path $stage 'docs') -Recurse
Copy-Item -LiteralPath (Join-Path $PSScriptRoot '使用说明.txt') -Destination (Join-Path $stage '使用说明.txt')
$exeHash = (Get-FileHash -LiteralPath (Join-Path $stage 'G75Battery.exe') -Algorithm SHA256).Hash.ToLowerInvariant()
"$exeHash  G75Battery.exe" | Set-Content -LiteralPath (Join-Path $stage 'SHA256SUMS.txt') -Encoding ASCII
$archive = Join-Path $DistDir ($name + '.zip')
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $archive -Force
$zipHash = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
@("$zipHash  $name.zip","$exeHash  G75Battery.exe") | Set-Content -LiteralPath (Join-Path $DistDir 'SHA256SUMS.txt') -Encoding ASCII
Write-Host "Portable package: $archive"
