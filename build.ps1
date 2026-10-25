<#
.SYNOPSIS
    One-shot Windows build of futari_recomp.

.DESCRIPTION
    Does what the README's "Build" section does by hand, in order:

      1. checks out the SDK's third-party submodules if they are missing, and
         fixes libmspack's git symlinks if they were checked out as text stubs;
      2. installs any missing tool through winget: Git, Python 3, CMake, Ninja,
         LLVM (clang-cl 20+) and Visual Studio 2022 Build Tools with the
         "Desktop development with C++" workload;
      3. enters the Visual Studio x64 developer environment, which is where
         clang-cl finds the MSVC headers and libs and CMake finds rc.exe;
      4. builds the ReXGlue CLI (the codegen tool) from the vendored SDK;
      5. extracts your game data into assets\ (and your DLC into dlc\) unless
         assets\ is already populated;
      6. runs scripts\regen.sh to generate the recompiled C++;
      7. configures with the win-amd64-<Config> preset and builds the exe plus
         the 8 module DLLs.

    Nothing here downloads game data. You supply the disc ISO or the console
    install folder from a copy you own -- docs\DUMPING.md explains how.

.PARAMETER GameSource
    Your "Mushihimesama Futari Ver 1.5" .iso, or the 435A07D7 folder copied off
    your console's storage. Optional when assets\default.xex already exists, or
    when exactly one .iso is sitting in the repo root.

.PARAMETER Config
    Release (default), RelWithDebInfo or Debug. Selects the matching
    win-amd64-* preset; the build lands in out\build\win-amd64-<config>\.

.PARAMETER NoInstall
    Never run winget. Report what is missing and stop instead.

.PARAMETER Run
    Launch futari.exe against assets\ once the build succeeds.

.EXAMPLE
    .\build.ps1 -GameSource "D:\dumps\Mushihimesama Futari Ver 1.5.iso" -Run

.EXAMPLE
    .\build.ps1
    assets\ is already populated; just (re)generate and (re)build.

.NOTES
    Git symlinks on Windows need Developer Mode (Settings > System > For
    developers) or an elevated shell. winget may show UAC prompts for the
    machine-wide installers (LLVM, CMake, Build Tools).
#>
[CmdletBinding()]
param(
    [string]$GameSource,
    [ValidateSet('Release', 'RelWithDebInfo', 'Debug')]
    [string]$Config = 'Release',
    [switch]$NoInstall,
    [switch]$Run
)

$ErrorActionPreference = 'Stop'
$Root = $PSScriptRoot
Set-Location $Root

# ---------------------------------------------------------------- helpers ---

function Step([string]$Message) { Write-Host "`n==> $Message" -ForegroundColor Cyan }
function Note([string]$Message) { Write-Host "    $Message" }
function Fail([string]$Message) {
    Write-Host "`n[build.ps1] $Message" -ForegroundColor Red
    exit 1
}

# Run an external command and stop the script on a non-zero exit code.
function Invoke-Checked([string]$Exe, [string[]]$Arguments) {
    Write-Host "    > $Exe $($Arguments -join ' ')" -ForegroundColor DarkGray
    & $Exe @Arguments
    if ($LASTEXITCODE -ne 0) { Fail "'$Exe' exited with code $LASTEXITCODE." }
}

# winget writes new installs into the registry PATH; this process only sees
# them after re-reading it.
function Update-PathFromRegistry {
    $machine = [Environment]::GetEnvironmentVariable('Path', 'Machine')
    $user = [Environment]::GetEnvironmentVariable('Path', 'User')
    $env:Path = "$machine;$user;$env:Path"
}

function Find-Exe([string]$Name) {
    $cmd = @(Get-Command $Name -CommandType Application -ErrorAction SilentlyContinue)
    if ($cmd.Count -gt 0) { return $cmd[0].Source }
    return $null
}

function Install-Tool([string]$Label, [string]$Id, [string[]]$Extra = @()) {
    if ($NoInstall) { Fail "$Label is missing (winget id $Id) and -NoInstall was given." }
    if (-not (Find-Exe 'winget')) {
        Fail "$Label is missing and winget is not available. Install it by hand (winget id: $Id) and re-run."
    }
    Step "Installing $Label (winget $Id)"
    $wingetArgs = @('install', '--id', $Id, '--exact', '--source', 'winget',
                    '--accept-source-agreements', '--accept-package-agreements',
                    '--disable-interactivity') + $Extra
    & winget @wingetArgs
    # 0x8A15002B: "already installed" -- only reachable if detection missed it.
    if ($LASTEXITCODE -ne 0 -and $LASTEXITCODE -ne -1978335189) {
        Fail "winget could not install $Label (exit code $LASTEXITCODE)."
    }
    Update-PathFromRegistry
}

# Python: prefer the py launcher; otherwise a real python.exe. The Microsoft
# Store alias is a stub that exits 9009, so test by running it.
function Find-Python {
    # Windows PowerShell turns redirected stderr into errors under 'Stop'; the
    # probes below may write there, so relax it for this function only.
    $ErrorActionPreference = 'Continue'
    foreach ($candidate in @(@('py', '-3'), @('python'))) {
        if (-not (Find-Exe $candidate[0])) { continue }
        $extra = @($candidate | Select-Object -Skip 1)
        $out = & $candidate[0] @extra --version 2>&1
        if ($LASTEXITCODE -eq 0 -and "$out" -match '^Python 3\.') { return $candidate }
    }
    # Output nothing (not $null) so the caller's @() wrapper sees an empty array.
    return
}

function Find-ClangCl {
    $candidates = @((Join-Path $env:ProgramFiles 'LLVM\bin\clang-cl.exe'), (Find-Exe 'clang-cl'))
    foreach ($c in $candidates) {
        if ($c -and (Test-Path $c)) { return $c }
    }
    return $null
}

function Get-ClangMajor([string]$ClangCl) {
    $ErrorActionPreference = 'Continue'
    $first = (& $ClangCl --version 2>&1 | Select-Object -First 1)
    if ("$first" -match 'clang version (\d+)') { return [int]$Matches[1] }
    return 0
}

# Visual Studio with the C++ toolset. Returns the install path or $null.
$VsCppComponent = 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64'
function Find-VsWhere {
    $p = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path $p) { return $p }
    return $null
}
function Find-VsWithCpp {
    $ErrorActionPreference = 'Continue'
    $vswhere = Find-VsWhere
    if (-not $vswhere) { return $null }
    $path = & $vswhere -latest -products * -requires $VsCppComponent -property installationPath 2>$null
    if ($path) { return "$path".Trim() }
    return $null
}

# ------------------------------------------------------------- 1. sources ---

Step 'Checking the repository'
if (-not (Test-Path (Join-Path $Root 'scripts\regen.sh'))) {
    Fail 'Run this from the futari_recomp checkout (scripts\regen.sh not found next to build.ps1).'
}

$git = Find-Exe 'git'
if (-not $git) { Install-Tool 'Git' 'Git.Git'; $git = Find-Exe 'git' }
if (-not $git) { Fail 'git is still not on PATH after installing. Open a new terminal and re-run.' }

# Long paths: harmless if already set, and the SDK's dependency trees are deep.
& $git config core.longpaths true | Out-Null

$sdk = Join-Path $Root 'thirdparty\rexglue-sdk'
if (-not (Test-Path (Join-Path $sdk 'thirdparty\fmt\CMakeLists.txt'))) {
    Note 'The SDK submodules are not checked out; fetching them (about 1 GB of history, once).'
    Invoke-Checked $git @('-c', 'core.symlinks=true', '-c', 'core.longpaths=true',
                          'submodule', 'update', '--init', '--recursive')
}

# libmspack uses git symlinks. Checked out without core.symlinks they become
# text stubs and the SDK build dies with "lzxd.c(1,1): error: expected identifier".
$mspack = Join-Path $sdk 'thirdparty\libmspack'
$linkPaths = @(& $git -C $mspack ls-files -s | Where-Object { $_ -match '^120000 ' } |
               ForEach-Object { ($_ -split "`t", 2)[1] })
function Get-BrokenLinks {
    @($linkPaths | Where-Object {
        $item = Get-Item -LiteralPath (Join-Path $mspack $_) -Force -ErrorAction SilentlyContinue
        -not ($item -and $item.LinkType -eq 'SymbolicLink')
    })
}
$broken = @(Get-BrokenLinks)
if ($broken.Count -gt 0) {
    Note "libmspack's symlinks are checked out as text stubs; re-checking them out as links."
    Invoke-Checked $git @('-C', $mspack, 'config', 'core.symlinks', 'true')
    foreach ($p in $broken) { Remove-Item -LiteralPath (Join-Path $mspack $p) -Force -ErrorAction SilentlyContinue }
    Invoke-Checked $git (@('-C', $mspack, 'checkout', '--') + $broken)
    if (@(Get-BrokenLinks).Count -gt 0) {
        Fail ('git could not create symlinks. Turn on Developer Mode (Settings > System > For developers) ' +
              'or run this from an elevated PowerShell, then re-run.')
    }
}

# --------------------------------------------------------------- 2. tools ---

Step 'Checking the toolchain'

# @() keeps a one-element result an array; a bare string would index by character.
$python = @(Find-Python)
if ($python.Count -eq 0) { Install-Tool 'Python 3' 'Python.Python.3.12'; $python = @(Find-Python) }
if ($python.Count -eq 0) { Fail 'Python 3 is still not usable after installing. Open a new terminal and re-run.' }
$pyExe = $python[0]
$pyPre = @($python | Select-Object -Skip 1)
Note "Python: $pyExe $($pyPre -join ' ')"

if (-not (Find-Exe 'cmake')) { Install-Tool 'CMake' 'Kitware.CMake' }
if (-not (Find-Exe 'cmake')) {
    $cm = Join-Path $env:ProgramFiles 'CMake\bin'
    if (Test-Path (Join-Path $cm 'cmake.exe')) { $env:Path = "$cm;$env:Path" }
}
if (-not (Find-Exe 'cmake')) { Fail 'cmake is not on PATH after installing. Open a new terminal and re-run.' }
Note "CMake: $(Find-Exe 'cmake')"

if (-not (Find-Exe 'ninja')) { Install-Tool 'Ninja' 'Ninja-build.Ninja' }
if (-not (Find-Exe 'ninja')) { Fail 'ninja is not on PATH after installing. Open a new terminal and re-run.' }
Note "Ninja: $(Find-Exe 'ninja')"

$clangCl = Find-ClangCl
if (-not $clangCl) { Install-Tool 'LLVM (clang-cl)' 'LLVM.LLVM'; $clangCl = Find-ClangCl }
if (-not $clangCl) { Fail 'clang-cl.exe was not found under C:\Program Files\LLVM\bin or on PATH.' }
$clangMajor = Get-ClangMajor $clangCl
if ($clangMajor -lt 20) {
    Fail "clang-cl at $clangCl is version $clangMajor; this project needs LLVM 20 or newer. Update it (winget upgrade --id LLVM.LLVM) and re-run."
}
Note "clang-cl: $clangCl (LLVM $clangMajor)"

$vsPath = Find-VsWithCpp
if (-not $vsPath) {
    $vswhere = Find-VsWhere
    $anyVs = $null
    if ($vswhere) { $anyVs = & $vswhere -latest -products * -property installationPath }
    if ($anyVs) {
        Fail ("Visual Studio is installed at $anyVs but without the C++ toolset. Open Visual Studio Installer, " +
              "Modify, and add 'Desktop development with C++' (it brings the MSVC tools and the Windows SDK), then re-run.")
    }
    Install-Tool 'Visual Studio 2022 Build Tools (C++ workload)' 'Microsoft.VisualStudio.2022.BuildTools' @(
        '--override', '--quiet --wait --norestart --nocache --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended')
    $vsPath = Find-VsWithCpp
}
if (-not $vsPath) { Fail 'The Visual Studio C++ toolset is still not found after installing.' }
Note "Visual Studio: $vsPath"

# bash comes with Git for Windows. Not System32\bash.exe: that is WSL, and
# regen.sh would then run inside Linux with none of these paths.
$gitRoot = Split-Path (Split-Path $git -Parent) -Parent
$bash = $null
foreach ($rel in @('bin\bash.exe', 'usr\bin\bash.exe')) {
    $p = Join-Path $gitRoot $rel
    if (Test-Path $p) { $bash = $p; break }
}
if (-not $bash) { Fail "Git's bash.exe was not found under $gitRoot. Install Git for Windows (winget id Git.Git)." }
Note "bash: $bash"

# ----------------------------------------------------------- 3. dev shell ---

Step 'Entering the Visual Studio x64 developer environment'
$devShell = Join-Path $vsPath 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll'
if (-not (Test-Path $devShell)) { Fail "Developer shell module not found at $devShell." }
Import-Module $devShell
Enter-VsDevShell -VsInstallPath $vsPath -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64 -no_logo' | Out-Null
if (-not (Find-Exe 'rc')) {
    Fail ('rc.exe is not on PATH inside the developer environment, so the Windows SDK component is missing. ' +
          'Add a Windows 10/11 SDK in Visual Studio Installer and re-run.')
}
Note "rc.exe: $(Find-Exe 'rc')"

# ------------------------------------------------------------ 4. SDK CLI ---

Step 'Building the ReXGlue CLI (codegen tool) from the vendored SDK'
$clangFwd = $clangCl.Replace('\', '/')
$sdkBuild = Join-Path $sdk 'build'
Invoke-Checked 'cmake' @('-S', $sdk, '-B', $sdkBuild, '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release',
                         "-DCMAKE_C_COMPILER=$clangFwd", "-DCMAKE_CXX_COMPILER=$clangFwd")
Invoke-Checked 'cmake' @('--build', $sdkBuild, '--target', 'rexglue')
$rexglue = Join-Path $sdk 'out\win-amd64\rexglue.exe'
if (-not (Test-Path $rexglue)) { Fail "The SDK built but $rexglue is missing." }

# ----------------------------------------------------------- 5. game data ---

Step 'Game data'
$assetsOk = (Test-Path (Join-Path $Root 'assets\default.xex')) -and (Test-Path (Join-Path $Root 'assets\media'))
if ($GameSource) {
    if (-not (Test-Path -LiteralPath $GameSource)) { Fail "GameSource not found: $GameSource" }
} elseif (-not $assetsOk) {
    $isos = @(Get-ChildItem -Path $Root -Filter *.iso -File)
    if ($isos.Count -eq 1) {
        $GameSource = $isos[0].FullName
        Note "Using $($isos[0].Name) from the repo root."
    } elseif ($isos.Count -gt 1) {
        Fail 'Several .iso files are in the repo root; pass the right one with -GameSource.'
    } else {
        Fail ('No game data. Pass -GameSource <your .iso or 435A07D7 folder>, or populate assets\ yourself so that ' +
              'assets\default.xex and assets\media\ exist. docs\DUMPING.md explains reading your own disc.')
    }
}
if ($GameSource) {
    Invoke-Checked $pyExe ($pyPre + @('scripts\extract_iso.py', $GameSource, '--out', 'assets', '--dlc-out', 'dlc'))
} else {
    Note 'assets\ is already populated; skipping extraction.'
}

# -------------------------------------------------------------- 6. codegen ---

Step 'Generating the recompiled C++ (scripts\regen.sh)'
$env:REXGLUE = 'thirdparty/rexglue-sdk/out/win-amd64/rexglue.exe'
Invoke-Checked $bash @('--noprofile', '--norc', 'scripts/regen.sh')

# ---------------------------------------------------- 7. configure + build ---

$preset = "win-amd64-$($Config.ToLower())"
Step "Configuring ($preset)"
$cfgArgs = @('--preset', $preset)
$defaultClang = Join-Path $env:ProgramFiles 'LLVM\bin\clang-cl.exe'
if ($clangCl -ne $defaultClang) {
    # The preset names the default LLVM install; point it at the one we found.
    $cfgArgs += @("-DCMAKE_C_COMPILER=$clangFwd", "-DCMAKE_CXX_COMPILER=$clangFwd")
}
Invoke-Checked 'cmake' $cfgArgs

Step 'Building futari.exe and the 8 module DLLs'
Invoke-Checked 'cmake' @('--build', '--preset', $preset)

$exe = Join-Path $Root "out\build\$preset\futari.exe"
if (-not (Test-Path $exe)) { Fail "Build finished but $exe is missing." }

Step 'Done'
Note "Executable: $exe"
Note "Run it with:  `"$exe`" --game_data_root `"$Root\assets`""
Note 'Logs go to logs\futari_NNN.log next to the exe.'
if ((Test-Path (Join-Path $Root 'dlc')) -and @(Get-ChildItem (Join-Path $Root 'dlc') -File).Count -gt 0) {
    Note 'DLC installs from a dlc\ folder next to the exe: copy dlc\ beside futari.exe to use it from the build directory.'
}

if ($Run) {
    Step 'Launching'
    Start-Process -FilePath $exe -ArgumentList @('--game_data_root', "`"$Root\assets`"") -WorkingDirectory (Split-Path $exe)
}
