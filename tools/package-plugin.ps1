<#
    Packages the Unreal plugin into a versioned, self-contained drop that can be
    copied into a project and submitted to Perforce.

        powershell -File tools\package-plugin.ps1
        powershell -File tools\package-plugin.ps1 -NoStrictIncludes -NoZip

    Why package rather than copy the source folder:

      - The output carries precompiled binaries, so the plugin also works in
        Blueprint-only projects that have no C++ toolchain.
      - Intermediate/, Binaries/ and Saved/ from the development working copy are
        excluded, so nobody submits half a gigabyte of build junk to Perforce.
      - The version in the .uplugin tells you which build a project has. A copied
        source folder tells you nothing.

    -StrictIncludes is on by default. It verifies every header compiles on its
    own rather than only inside a unity build, which is the difference between a
    plugin that works here and one that works in someone else's project.
#>
[CmdletBinding()]
param(
    [string] $EnginePath,
    [string] $OutputDir,
    [string[]] $TargetPlatforms = @('Win64'),
    [switch] $NoStrictIncludes,
    [switch] $NoZip
)

$ErrorActionPreference = 'Stop'

$Root       = Split-Path -Parent $PSScriptRoot
$PluginFile = Join-Path $Root 'unreal\Plugins\FigmaTokenBridge\FigmaTokenBridge.uplugin'

if (-not (Test-Path $PluginFile)) { throw "Plugin descriptor not found: $PluginFile" }

function Find-Engine {
    param([string] $Explicit)
    if ($Explicit) { return $Explicit }
    $candidates = @('C:\Program Files\Epic Games\UE_5.8', 'D:\Program Files\Epic Games\UE_5.8')
    try {
        Get-ItemProperty 'HKLM:\SOFTWARE\EpicGames\Unreal Engine\*' -ErrorAction Stop |
            ForEach-Object { if ($_.InstalledDirectory) { $candidates += $_.InstalledDirectory } }
    } catch { }
    foreach ($c in $candidates) {
        if ($c -and (Test-Path (Join-Path $c 'Engine\Build\BatchFiles\RunUAT.bat'))) { return $c }
    }
    throw 'Could not find Unreal Engine 5.8. Pass -EnginePath "C:\Path\To\UE_5.8".'
}

$Engine = Find-Engine -Explicit $EnginePath
$RunUAT = Join-Path $Engine 'Engine\Build\BatchFiles\RunUAT.bat'

# Version and engine version come from the descriptor, so the artifact name can
# never disagree with what is inside it.
$descriptor    = Get-Content $PluginFile -Raw | ConvertFrom-Json
$version       = $descriptor.VersionName
$engineVersion = $descriptor.EngineVersion
if (-not $version) { throw "VersionName is missing from $PluginFile" }

$stamp   = "FigmaTokenBridge-$version-UE$engineVersion"
$Staging = if ($OutputDir) { $OutputDir } else { Join-Path $Root "dist\$stamp" }

Write-Host "Engine:   $Engine"
Write-Host "Plugin:   $PluginFile"
Write-Host "Version:  $version  (engine $engineVersion)"
Write-Host "Output:   $Staging"
Write-Host ""

if (Test-Path $Staging) { Remove-Item $Staging -Recurse -Force }
New-Item -ItemType Directory -Path $Staging -Force | Out-Null

$uatArgs = @(
    'BuildPlugin'
    "-Plugin=$PluginFile"
    "-Package=$Staging"
    "-TargetPlatforms=$($TargetPlatforms -join '+')"
)
if (-not $NoStrictIncludes) { $uatArgs += '-StrictIncludes' }

Write-Host "=== RunUAT BuildPlugin ===`n"
$uatOut = & $RunUAT @uatArgs 2>&1
$uatExit = $LASTEXITCODE
$uatOut | ForEach-Object { $_.ToString() }

if ($uatExit -ne 0) {
    Write-Host ""
    Write-Host "=== Packaging errors ===" -ForegroundColor Red
    $uatOut |
        Select-String -Pattern 'error C|error LNK|ERROR:|Error:|fatal error' |
        ForEach-Object { Write-Host "  $($_.Line)" -ForegroundColor Red }
    throw "BuildPlugin failed ($uatExit)."
}

# BuildPlugin already excludes Saved/ and the host project, but Intermediate is
# left behind and is pure noise in a distributed drop.
$intermediate = Join-Path $Staging 'Intermediate'
if (Test-Path $intermediate) { Remove-Item $intermediate -Recurse -Force }

$size = [math]::Round((Get-ChildItem $Staging -Recurse -File | Measure-Object Length -Sum).Sum / 1MB, 1)
Write-Host ""
Write-Host "Packaged $stamp  ($size MB)"

if (-not $NoZip) {
    $zip = Join-Path $Root "dist\$stamp.zip"
    if (Test-Path $zip) { Remove-Item $zip -Force }
    Compress-Archive -Path (Join-Path $Staging '*') -DestinationPath $zip
    $zipSize = [math]::Round((Get-Item $zip).Length / 1MB, 1)
    Write-Host "Zipped   $zip  ($zipSize MB)"
}

Write-Host ""
Write-Host "To install into a project:"
Write-Host "  1. Copy the packaged FigmaTokenBridge folder into <Project>/Plugins/"
Write-Host "  2. Submit it to Perforce with the rest of the project"
Write-Host "  3. Enable the plugin, and set Project Settings > Plugins > Figma Token Bridge"
