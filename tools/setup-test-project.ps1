<#
    Prepares unreal/TestProject so the Figma Token Bridge plugin can be built.

    The plugin lives at unreal/Plugins/FigmaTokenBridge because that is where it
    is copied from when adding it to a real project. Unreal, though, only picks
    up plugins under <Project>/Plugins. Rather than keep two copies in sync, this
    links one into the other with a directory junction — a plain-file copy would
    drift the moment anyone edited the wrong one, and junctions need no admin
    rights on Windows.

        pwsh tools/setup-test-project.ps1
        pwsh tools/setup-test-project.ps1 -GenerateProjectFiles   # also make the .sln
#>
[CmdletBinding()]
param(
    [string] $EnginePath,
    [switch] $GenerateProjectFiles
)

$ErrorActionPreference = 'Stop'

$Root      = Split-Path -Parent $PSScriptRoot
$PluginSrc = Join-Path $Root 'unreal\Plugins\FigmaTokenBridge'
$Project   = Join-Path $Root 'unreal\TestProject'
$UProject  = Join-Path $Project 'FigmaBridgeTest.uproject'
$LinkDir   = Join-Path $Project 'Plugins'
$Link      = Join-Path $LinkDir 'FigmaTokenBridge'

function Find-Engine {
    param([string] $Explicit)

    if ($Explicit) {
        if (-not (Test-Path (Join-Path $Explicit 'Engine\Build\BatchFiles\Build.bat'))) {
            throw "No Unreal engine at '$Explicit' (expected Engine\Build\BatchFiles\Build.bat)."
        }
        return $Explicit
    }

    # Launcher installs first, then source builds registered in the registry.
    $candidates = @(
        'C:\Program Files\Epic Games\UE_5.8'
        'D:\Program Files\Epic Games\UE_5.8'
    )
    try {
        Get-ItemProperty 'HKLM:\SOFTWARE\EpicGames\Unreal Engine\*' -ErrorAction Stop |
            ForEach-Object { if ($_.InstalledDirectory) { $candidates += $_.InstalledDirectory } }
    } catch { }

    foreach ($c in $candidates) {
        if ($c -and (Test-Path (Join-Path $c 'Engine\Build\BatchFiles\Build.bat'))) { return $c }
    }
    throw 'Could not find Unreal Engine 5.8. Pass -EnginePath "C:\Path\To\UE_5.8".'
}

if (-not (Test-Path $PluginSrc)) { throw "Plugin source is missing: $PluginSrc" }
if (-not (Test-Path $UProject))  { throw "Test project is missing: $UProject" }

# --- the plugin junction ---------------------------------------------------

New-Item -ItemType Directory -Path $LinkDir -Force | Out-Null

$existing = Get-Item $Link -ErrorAction SilentlyContinue
if ($existing) {
    if ($existing.LinkType -eq 'Junction') {
        Write-Host "Junction already present: $Link"
    } else {
        throw "$Link exists but is not a junction. Delete it and re-run, or you will be editing a stale copy of the plugin."
    }
} else {
    New-Item -ItemType Junction -Path $Link -Target $PluginSrc | Out-Null
    Write-Host "Linked $Link -> $PluginSrc"
}

# --- engine ----------------------------------------------------------------

$Engine = Find-Engine -Explicit $EnginePath
Write-Host "Engine: $Engine"

# UBT needs a C++ toolchain. Saying so here is much clearer than the wall of
# text UBT produces when it cannot find one.
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$msvc = $null
if (Test-Path $vswhere) {
    $msvc = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
}
if (-not $msvc) {
    Write-Warning @'
No MSVC toolchain found, so the C++ cannot be compiled yet.
Install Visual Studio with the "Desktop development with C++" workload, plus:
  - MSVC v14.3x C++ x64/x86 build tools
  - Windows 11 SDK
  - .NET desktop development (UnrealBuildTool needs it for project files)
Then re-run this script.
'@
} else {
    Write-Host "MSVC: $msvc"
}

if ($GenerateProjectFiles) {
    $ubt = Join-Path $Engine 'Engine\Build\BatchFiles\Build.bat'
    Write-Host "Generating project files..."
    & $ubt -projectfiles -project="$UProject" -game -engine -progress
    if ($LASTEXITCODE -ne 0) { throw "Project file generation failed ($LASTEXITCODE)." }
}

Write-Host ''
Write-Host 'Ready. Next:  pwsh tools/build-unreal.ps1'
