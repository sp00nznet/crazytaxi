# Quick start for Crazy Taxi (README "Getting Started"). Run through Setup.cmd.
#
# Runs exactly the commands the Step by step section documents, skipping any
# step whose output already exists, so a rerun after a failure resumes. Asks
# before installing anything. Never downloads game data: the disc is yours.
# Windows PowerShell 5.1 compatible (no ?:, ??, or &&).
param([string]$Disc)

$ErrorActionPreference = 'Stop'
$Root = Split-Path $PSScriptRoot -Parent
Set-Location $Root
$Log = Join-Path $Root 'setup.log'
"=== setup $(Get-Date -Format s) ===" | Set-Content $Log

function Say($msg)  { Write-Host "-> $msg" -ForegroundColor Cyan; "-> $msg" | Add-Content $Log }
function Fail($msg) {
    Write-Host ""
    Write-Host "Setup stopped: $msg" -ForegroundColor Red
    Write-Host "Details are in $Log"
    exit 1
}
function Ask($q) { (Read-Host "$q [y/N]") -match '^(y|yes)$' }
function RefreshPath {
    $env:Path = [Environment]::GetEnvironmentVariable('Path', 'Machine') + ';' +
                [Environment]::GetEnvironmentVariable('Path', 'User')
}
# Run a program with its output going to the log, not the screen (a full build
# prints thousands of warning lines). Start-Process doesn't quote arguments in
# 5.1, so anything with a space gets quoted here.
function Run($what, $exe, [string[]]$argv) {
    Say $what
    $q = $argv | ForEach-Object { if ($_ -match '\s') { '"' + $_ + '"' } else { $_ } }
    "   $exe $($q -join ' ')" | Add-Content $Log
    # Not -Wait: that also waits for every child, and MSBuild leaves node
    # processes running for ~15 minutes after cmake is done. Touching .Handle
    # first is what makes ExitCode readable in 5.1.
    $p = Start-Process -FilePath $exe -ArgumentList $q -NoNewWindow -PassThru `
         -RedirectStandardOutput "$Log.out" -RedirectStandardError "$Log.err"
    $null = $p.Handle
    $p.WaitForExit()
    Get-Content "$Log.out", "$Log.err" | Add-Content $Log
    Remove-Item "$Log.out", "$Log.err" -ErrorAction SilentlyContinue
    if ($p.ExitCode -ne 0) { Fail "$what failed (exit code $($p.ExitCode))." }
}
function WingetInstall($name, $id, $size, [string[]]$extra) {
    if (-not (Get-Command winget -ErrorAction SilentlyContinue)) {
        Fail "$name is missing and winget isn't available to install it. Install $name yourself and run Setup again."
    }
    if (-not (Ask "$name is needed and not installed. Install it with winget ($size)?")) {
        Fail "$name is required. Install it and run Setup again."
    }
    Run "Installing $name" 'winget' (@('install', '-e', '--id', $id,
        '--accept-package-agreements', '--accept-source-agreements') + $extra)
    RefreshPath
}

Write-Host "Crazy Taxi setup. Your own disc is needed; nothing from the game is downloaded."
Write-Host ""

# --- 1. Prerequisites -------------------------------------------------------
Say "Checking prerequisites"

if (-not (Get-Command py -ErrorAction SilentlyContinue)) {
    WingetInstall 'Python 3' 'Python.Python.3.12' 'about 30 MB'
}
if (-not (Get-Command py -ErrorAction SilentlyContinue)) {
    Fail "Python was installed but isn't visible yet. Close this window and run Setup again."
}

if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
    WingetInstall 'Git' 'Git.Git' 'about 60 MB'
}

if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    WingetInstall 'CMake' 'Kitware.CMake' 'about 35 MB'
}

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
function FindVS {
    if (-not (Test-Path $vswhere)) { return $null }
    $v = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
         -property installationVersion
    if ($v) { return [int]($v.Split('.')[0]) }
    return $null
}
$vsMajor = FindVS
if (-not $vsMajor) {
    WingetInstall 'Visual Studio 2022 Build Tools (C++)' 'Microsoft.VisualStudio.2022.BuildTools' `
        'about 4 GB' @('--override', '--wait --passive --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended')
    $vsMajor = FindVS
    if (-not $vsMajor) { Fail "The C++ build tools didn't install. Install 'Desktop development with C++' from the Visual Studio Installer and run Setup again." }
}
$years = @{ 16 = '2019'; 17 = '2022'; 18 = '2026' }
if (-not $years.ContainsKey($vsMajor)) { Fail "Visual Studio version $vsMajor isn't one this setup knows. Use Visual Studio 2022." }
$generator = "Visual Studio $vsMajor $($years[$vsMajor])"

# vcpkg supplies SDL2 and GLEW. Reuse one that's already on the machine.
$vcpkg = $null
foreach ($c in @($env:VCPKG_ROOT, 'C:\vcpkg', (Join-Path $Root '.deps\vcpkg'))) {
    if ($c -and (Test-Path (Join-Path $c 'vcpkg.exe'))) { $vcpkg = $c; break }
}
if (-not $vcpkg) {
    $vcpkg = Join-Path $Root '.deps\vcpkg'
    if (-not (Ask "vcpkg (for SDL2 and GLEW) isn't installed. Download it into .deps\vcpkg (about 1 GB once built)?")) {
        Fail "vcpkg is required for the windowed build. Install it, or set VCPKG_ROOT, and run Setup again."
    }
    if (-not (Test-Path $vcpkg)) {
        Run 'Downloading vcpkg' 'git' @('clone', '--depth', '1', 'https://github.com/microsoft/vcpkg', $vcpkg)
    }
    Run 'Setting up vcpkg' (Join-Path $vcpkg 'bootstrap-vcpkg.bat') @('-disableMetrics')
}
Run 'Installing SDL2 and GLEW (first time takes a few minutes)' (Join-Path $vcpkg 'vcpkg.exe') @('install', 'sdl2:x64-windows', 'glew:x64-windows')

# --- 2. dcrecomp (the recompiler) -------------------------------------------
if (-not (Test-Path 'dcrecomp\tools\static_recompile.py')) {
    if (Test-Path '.git') {
        Run 'Fetching dcrecomp' 'git' @('submodule', 'update', '--init')
    } else {
        # A plain zip download has an empty dcrecomp\ folder. Fetch the exact
        # commit this repo points at, so the zip and a clone build the same.
        Say 'Fetching dcrecomp'
        try {
            $sha = (Invoke-RestMethod 'https://api.github.com/repos/sp00nznet/crazytaxi/contents/dcrecomp').sha
            $zip = Join-Path $env:TEMP "dcrecomp-$sha.zip"
            Invoke-WebRequest "https://github.com/sp00nznet/dcrecomp/archive/$sha.zip" -OutFile $zip -UseBasicParsing
            $tmp = Join-Path $env:TEMP "dcrecomp-$sha"
            Expand-Archive $zip $tmp -Force
            New-Item -ItemType Directory -Force 'dcrecomp' | Out-Null
            Copy-Item "$tmp\dcrecomp-$sha\*" 'dcrecomp' -Recurse -Force
        } catch {
            $_ | Out-String | Add-Content $Log
            Fail "Couldn't download dcrecomp. Check your internet connection and run Setup again."
        }
    }
}

# --- 3. Extract the disc ----------------------------------------------------
if (-not (Test-Path 'disc_extract\1ST_READ.BIN')) {
    if (-not $Disc) {
        $found = @(Get-ChildItem $Root -File | Where-Object { $_.Extension -in '.cue', '.gdi' })
        if ($found.Count -eq 1) { $Disc = $found[0].FullName; Say "Using disc $Disc" }
    }
    if (-not $Disc) {
        $Disc = (Read-Host "Path to your Crazy Taxi disc (.cue or .gdi)").Trim('"', ' ')
    }
    if (-not (Test-Path $Disc)) { Fail "No file at '$Disc'. Point Setup at your disc's .cue or .gdi file." }
    Run 'Extracting the disc' 'py' @('-3', 'dcrecomp\tools\extract_gdi.py', $Disc, 'disc_extract')
    if (-not (Test-Path 'disc_extract\1ST_READ.BIN')) {
        Fail "The disc extracted but has no 1ST_READ.BIN. Is it Crazy Taxi (US)?"
    }
}

# --- 4. Translate the game to C ---------------------------------------------
if (-not (Test-Path 'src\game\game_stubs.c')) {
    Run 'Translating the game to C' 'py' @('-3', 'dcrecomp\tools\static_recompile.py',
        'disc_extract\1ST_READ.BIN', 'src\game', 'include\game', '--entries', 'entries.txt')
    Run 'Generating stubs' 'py' @('-3', 'tools\generate_stubs.py')
}

# --- 5. Build ---------------------------------------------------------------
Run "Configuring the build ($generator)" 'cmake' @('-S', '.', '-B', 'build', '-G', $generator, '-A', 'x64',
    "-DCMAKE_TOOLCHAIN_FILE=$vcpkg\scripts\buildsystems\vcpkg.cmake")
Run 'Building (10-20 minutes the first time)' 'cmake' @('--build', 'build', '--config', 'Release', '--parallel')
if (-not (Test-Path 'build\Release\crazytaxi.exe')) { Fail "The build finished but crazytaxi.exe isn't in build\Release." }

# --- 6. Launcher ------------------------------------------------------------
"@echo off`r`ncd /d `"%~dp0`"`r`n`"build\Release\crazytaxi.exe`" %*`r`n" |
    Set-Content -Encoding ASCII (Join-Path $Root 'Play Crazy Taxi.cmd')

Write-Host ""
Write-Host "Done. Double-click 'Play Crazy Taxi.cmd' to run it." -ForegroundColor Green
Write-Host "It boots and initialises but doesn't draw a frame yet - see README, Status."
