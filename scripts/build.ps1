[CmdletBinding()]
param(
    [ValidateSet('Auto','MSVC','MinGW')][string]$Toolchain = 'Auto',
    [string]$MinGWBin = $env:MINGW_BIN,
    [string]$OutDir
)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$sourceDir = Join-Path $repoRoot 'src'
if (-not $OutDir) { $OutDir = Join-Path $repoRoot 'build' }
$OutDir = [IO.Path]::GetFullPath($OutDir)
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

function Invoke-Checked([string]$Program, [string[]]$Arguments) {
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program failed with exit code $LASTEXITCODE" }
}

if (-not $MinGWBin) {
    $gccCommand = Get-Command gcc.exe -ErrorAction SilentlyContinue
    if ($gccCommand) { $MinGWBin = Split-Path -Parent $gccCommand.Source }
    elseif (Test-Path -LiteralPath 'C:\mingw64\bin\gcc.exe') { $MinGWBin = 'C:\mingw64\bin' }
}
if ($Toolchain -eq 'Auto') {
    if ($MinGWBin -and (Test-Path -LiteralPath (Join-Path $MinGWBin 'windres.exe'))) { $Toolchain = 'MinGW' }
    else { $Toolchain = 'MSVC' }
}

$originalPath = $env:PATH
Push-Location $sourceDir
try {
    if ($Toolchain -eq 'MinGW') {
        if (-not $MinGWBin) { throw 'Install MinGW-w64 or pass -MinGWBin <directory containing gcc.exe and windres.exe>.' }
        $gcc = Join-Path $MinGWBin 'gcc.exe'
        $windres = Join-Path $MinGWBin 'windres.exe'
        if (-not (Test-Path -LiteralPath $gcc) -or -not (Test-Path -LiteralPath $windres)) { throw 'gcc.exe and windres.exe are required.' }
        $env:PATH = $MinGWBin + ';' + $env:PATH
        $machine = & $gcc -dumpmachine
        if ($machine -notmatch '^x86_64-') { throw "Expected an x64 compiler, found $machine" }
        $resource = Join-Path $OutDir 'G75Battery-res.o'
        Invoke-Checked $windres @('G75Battery.rc','-O','coff','-o',$resource)
        Invoke-Checked $gcc @('-std=c11','-Os','-s','-Wall','-Wextra','-municode','-mwindows','-static-libgcc','-finput-charset=UTF-8','-fexec-charset=UTF-8','G75Battery.c',$resource,'-o',(Join-Path $OutDir 'G75Battery.exe'),'-lsetupapi','-lhid','-lshell32','-ladvapi32','-luser32','-lgdi32','-lkernel32','-lcfgmgr32','-Wl,--nxcompat,--dynamicbase,--high-entropy-va')
    } else {
        if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue) -or -not (Get-Command rc.exe -ErrorAction SilentlyContinue)) {
            $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
            if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Install Visual Studio C++ Build Tools and Windows SDK, or use MinGW-w64.' }
            $vsPath = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
            if (-not $vsPath) { throw 'Visual Studio C++ tools were not found.' }
            $devCmd = Join-Path $vsPath 'Common7\Tools\VsDevCmd.bat'
            $envScript = Join-Path $OutDir 'import-msvc-env.cmd'
            @('@echo off',('call "' + $devCmd + '" -arch=x64 -host_arch=x64 >nul'),'if errorlevel 1 exit /b 1','set') | Set-Content -LiteralPath $envScript -Encoding ASCII
            $environmentLines = & $env:ComSpec /d /c $envScript
            if ($LASTEXITCODE -ne 0) { throw 'Could not initialize Visual Studio environment.' }
            foreach ($line in $environmentLines) {
                $equals = $line.IndexOf('=')
                if ($equals -gt 0) { [Environment]::SetEnvironmentVariable($line.Substring(0,$equals),$line.Substring($equals+1),'Process') }
            }
        }
        if (-not (Get-Command rc.exe -ErrorAction SilentlyContinue)) { throw 'Windows SDK resource compiler rc.exe was not found.' }
        $resource = Join-Path $OutDir 'G75Battery.res'
        Invoke-Checked 'rc.exe' @('/nologo','/fo',$resource,'G75Battery.rc')
        Invoke-Checked 'cl.exe' @('/nologo','/utf-8','/std:c11','/O1','/MT','/W4','/D_CRT_SECURE_NO_WARNINGS',('/Fo' + (Join-Path $OutDir 'G75Battery.obj')),'G75Battery.c',$resource,('/Fe' + (Join-Path $OutDir 'G75Battery.exe')),'/link','/SUBSYSTEM:WINDOWS','/MACHINE:X64','/DYNAMICBASE','/HIGHENTROPYVA','/NXCOMPAT','setupapi.lib','hid.lib','shell32.lib','advapi32.lib','user32.lib','gdi32.lib','kernel32.lib','cfgmgr32.lib')
    }
} finally {
    Pop-Location
    $env:PATH = $originalPath
}
$exe = Join-Path $OutDir 'G75Battery.exe'
$test = Start-Process -FilePath $exe -ArgumentList '--self-test' -WindowStyle Hidden -Wait -PassThru
if ($test.ExitCode -ne 0) { throw "Offline self-test failed: $($test.ExitCode)" }
Write-Host "Built and self-tested: $exe"
