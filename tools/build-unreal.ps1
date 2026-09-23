<#
    Builds the Figma Token Bridge plugin inside unreal/TestProject and, unless
    told otherwise, runs its automation tests headless.

        pwsh tools/build-unreal.ps1              # build, then test
        pwsh tools/build-unreal.ps1 -NoTests     # build only
        pwsh tools/build-unreal.ps1 -TestsOnly   # skip the build

    Exits non-zero if either the build or any test fails, so it is usable as a
    CI step as-is.
#>
[CmdletBinding()]
param(
    [string] $EnginePath,
    [ValidateSet('Development', 'DebugGame', 'Shipping')]
    [string] $Configuration = 'Development',
    [switch] $NoTests,
    [switch] $TestsOnly
)

$ErrorActionPreference = 'Stop'

$Root     = Split-Path -Parent $PSScriptRoot
$Project  = Join-Path $Root 'unreal\TestProject'
$UProject = Join-Path $Project 'FigmaBridgeTest.uproject'
$Link     = Join-Path $Project 'Plugins\FigmaTokenBridge'

if (-not (Test-Path $UProject)) { throw "Test project is missing: $UProject" }
if (-not (Test-Path $Link)) {
    throw "The plugin is not linked into the test project. Run: pwsh tools/setup-test-project.ps1"
}

function Find-Engine {
    param([string] $Explicit)
    if ($Explicit) { return $Explicit }
    $candidates = @('C:\Program Files\Epic Games\UE_5.8', 'D:\Program Files\Epic Games\UE_5.8')
    try {
        Get-ItemProperty 'HKLM:\SOFTWARE\EpicGames\Unreal Engine\*' -ErrorAction Stop |
            ForEach-Object { if ($_.InstalledDirectory) { $candidates += $_.InstalledDirectory } }
    } catch { }
    foreach ($c in $candidates) {
        if ($c -and (Test-Path (Join-Path $c 'Engine\Build\BatchFiles\Build.bat'))) { return $c }
    }
    throw 'Could not find Unreal Engine 5.8. Pass -EnginePath "C:\Path\To\UE_5.8".'
}

$Engine = Find-Engine -Explicit $EnginePath
$Build  = Join-Path $Engine 'Engine\Build\BatchFiles\Build.bat'
$EdCmd  = Join-Path $Engine 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
foreach ($p in @($Build, $EdCmd)) {
    if (-not (Test-Path $p)) { throw "Missing engine tool: $p" }
}

Write-Host "Engine:  $Engine"
Write-Host "Project: $UProject"

# --- build -----------------------------------------------------------------

if (-not $TestsOnly) {
    Write-Host "`n=== Building FigmaBridgeTestEditor Win64 $Configuration ===`n"
    # Capture rather than stream, so a failure can be replayed as just the
    # error lines. Streaming it straight through buries three compiler errors in
    # several hundred lines of UBA chatter.
    $buildOut = & $Build FigmaBridgeTestEditor Win64 $Configuration -project="$UProject" -WaitMutex 2>&1
    $buildExit = $LASTEXITCODE
    $buildOut | ForEach-Object { $_.ToString() }

    if ($buildExit -ne 0) {
        Write-Host ""
        Write-Host "=== Build errors ===" -ForegroundColor Red
        $buildOut |
            Select-String -Pattern "error C|error LNK|Error:|fatal error" |
            ForEach-Object { Write-Host "  $($_.Line)" -ForegroundColor Red }
        throw "Build failed ($buildExit)."
    }
    Write-Host "`nBuild succeeded."
}

if ($NoTests) { return }

# --- tests -----------------------------------------------------------------
#
# The editor writes its automation report to a directory we then read back,
# because the console output interleaves with everything else the editor logs
# and is not something you want to grep for a pass/fail.

$ReportDir = Join-Path $Project 'Saved\AutomationReport'
if (Test-Path $ReportDir) { Remove-Item $ReportDir -Recurse -Force }
New-Item -ItemType Directory -Path $ReportDir -Force | Out-Null

Write-Host "`n=== Running automation tests: FigmaTokenBridge ===`n"

& $EdCmd "$UProject" `
    -ExecCmds="Automation RunTests FigmaTokenBridge; Quit" `
    -ReportExportPath="$ReportDir" `
    -unattended -nopause -nosplash -nullrhi -NoSound -stdout -FullStdOutLogOutput

$editorExit = $LASTEXITCODE

$indexPath = Join-Path $ReportDir 'index.json'
if (-not (Test-Path $indexPath)) {
    throw "No automation report at $indexPath (editor exit code $editorExit). Check the log above."
}

$report = Get-Content $indexPath -Raw | ConvertFrom-Json

Write-Host "`n=== Results ===`n"
$failed = 0
foreach ($t in $report.tests) {
    $state = $t.state
    $warnings = @($t.entries | Where-Object { $_.event.type -eq 'Warning' })

    if ($state -eq 'Success') {
        $note = ''
        if ($warnings.Count -gt 0) { $note = "   ($($warnings.Count) warning(s))" }
        Write-Host "  ok   $($t.fullTestPath)$note"
        # Some warnings are the point of the test — proving an unknown token
        # falls back to magenta logs one deliberately — so they are surfaced
        # rather than hidden, but they are not failures.
        foreach ($w in $warnings) { Write-Host "         ! $($w.event.message)" }
    } else {
        $failed++
        Write-Host " FAIL  $($t.fullTestPath)"
        foreach ($e in $t.entries) {
            if ($e.event.type -ne 'Info') {
                Write-Host "         $($e.event.message)"
            }
        }
    }
}

Write-Host ""
# succeeded and succeededWithWarnings are separate buckets in the report, so
# printing only the first makes the total disagree with the rows above.
$passed = $report.succeeded + $report.succeededWithWarnings
Write-Host "$passed passed ($($report.succeededWithWarnings) with warnings), $($report.failed) failed, $($report.notRun) not run"

if ($failed -gt 0 -or $report.failed -gt 0) {
    throw "$($report.failed) automation test(s) failed."
}
if ($passed -eq 0) {
    throw "No tests ran. Is the FigmaTokenBridgeEditor module built and loaded?"
}

Write-Host "`nAll automation tests passed."
