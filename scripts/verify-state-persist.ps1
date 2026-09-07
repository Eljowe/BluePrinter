# Verify BluePrinter plugin-state persistence across close/open cycles.
# Run from PowerShell 5.1+ as a normal user. No admin needed.
#
# What it checks:
#   A) Saved plugin states survive a close/open cycle even for BYPASSED slots
#      (the "Neural DSP X amps come back in default state" bug).
#   B) Quitting mid-restore (splash still showing) does NOT make the next
#      launch load every plugin with defaults (the marker false-positive).
#
# The script inspects %APPDATA%\Retrokielto\BluePrinter.properties and
# %APPDATA%\BluePrinter\BluePrinter.settings; you drive the app itself.
# NOTE: keep this file ASCII-only (PowerShell 5.1 mis-parses non-ASCII
# without a BOM).

$ErrorActionPreference = 'Stop'

$retro    = Join-Path $env:APPDATA 'Retrokielto'
$propsPath = Join-Path $retro 'BluePrinter.properties'
$settingsPath = Join-Path $env:APPDATA 'BluePrinter\BluePrinter.settings'

function Get-Prop([xml]$Xml, [string]$Name) {
    $node = $Xml.PROPERTIES.VALUE | Where-Object name -eq $Name
    if ($null -eq $node) { return $null }
    return $node.val
}

function Get-BlobInfo([xml]$Xml) {
    $json = [System.Net.WebUtility]::HtmlDecode((Get-Prop $Xml 'pluginChains'))
    if (-not $json) { return @() }
    $data = $json | ConvertFrom-Json
    $rows = @()
    foreach ($chain in $data.chains) {
        foreach ($slot in $chain.slots) {
            $st = [string]$slot.state
            $hash = if ($st) { (Get-FileHash -InputStream ([IO.MemoryStream][Text.Encoding]::UTF8.GetBytes($st)) -Algorithm SHA256).Hash.Substring(0,12) } else { '(empty)' }
            $rows += [pscustomobject]@{
                Chain    = $chain.name
                Plugin   = $slot.name
                Bypassed = [bool]$slot.bypassed
                StateLen = $st.Length
                StateHash = $hash
            }
        }
    }
    return $rows
}

function Show-State {
    if (-not (Test-Path $propsPath)) { Write-Host "NO $propsPath - nothing saved yet."; return }
    $xml = [xml](Get-Content $propsPath -Raw)
    $marker     = Get-Prop $xml 'chainRestoreCrashed'
    $markerTime = [int64](Get-Prop $xml 'chainRestoreMarkerTime')
    $loadOp     = Get-Prop $xml 'lastPluginLoadOp'
    $quarantine = Get-Prop $xml 'pluginQuarantine'
    $settingsMtime = if (Test-Path $settingsPath) { (Get-Item $settingsPath).LastWriteTimeUtc } else { $null }
    $markerUtc = if ($markerTime -gt 0) { ([DateTimeOffset]::FromUnixTimeMilliseconds($markerTime)).UtcDateTime } else { $null }

    Write-Host ''
    Write-Host '--- Persistence state ---'
    Write-Host ("chainRestoreCrashed    = '{0}'  (true = previous session's restore never drained)" -f $marker)
    Write-Host ("chainRestoreMarkerTime = {0}  {1}" -f $markerTime, $markerUtc)
    Write-Host ("lastPluginLoadOp       = '{0}'" -f $loadOp)
    Write-Host ("pluginQuarantine       = {0}" -f $quarantine)
    Write-Host ("BluePrinter.settings   = {0}  (clean-exit anchor)" -f $settingsMtime)

    if ($marker -eq 'true' -and $markerTime -gt 0 -and $settingsMtime) {
        if ($settingsMtime -gt $markerUtc) {
            Write-Host '  -> marker STALE: a clean exit postdates it, state blobs WILL be restored. (fixed behaviour)'
        } else {
            Write-Host '  -> marker FRESH: no clean exit since it was written (crash?), state blobs will be SKIPPED -> defaults.'
        }
    }

    Write-Host ''
    Write-Host '--- Slots on disk ---'
    Get-BlobInfo $xml | Format-Table -AutoSize | Out-String | Write-Host
}

function Wait-Key([string]$Prompt) {
    Write-Host $Prompt -NoNewline
    Read-Host | Out-Null
}

Show-State

Write-Host ''
Write-Host '==================================================================='
Write-Host ' TEST A - bypassed slots must restore their saved state'
Write-Host '==================================================================='
Write-Host '1. Start BluePrinter and wait for the splash to clear (restore done).'
Write-Host '2. On the Guitar chain, UN-BYPASS the first Archetype X slot and'
Write-Host '   play / open its editor. The SAVED tone must be there - not a'
Write-Host '   default amp (this was the bug: bypassed slots loaded defaults).'
Wait-Key '3. When verified, press Enter to quit the test and CLOSE BluePrinter.'

Show-State

Write-Host ''
Write-Host '==================================================================='
Write-Host ' TEST B - quitting mid-restore must not nuke the states'
Write-Host '==================================================================='
Write-Host '1. Start BluePrinter and IMMEDIATELY close it while the splash is'
Write-Host '   still showing "restoring..." (before the pending count hits 0).'
Wait-Key '2. Press Enter once it is fully closed.'

$xmlAfter1 = [xml](Get-Content $propsPath -Raw)
$markerAfter1 = Get-Prop $xmlAfter1 'chainRestoreCrashed'
Write-Host ("chainRestoreCrashed right after the quick quit = '{0}' (expected 'true' -" -f $markerAfter1)
Write-Host '   the restore never drained; the FIX is that the next launch ignores it)'

Write-Host ''
Write-Host '3. Start BluePrinter again. Wait for the full restore to finish.'
Write-Host '4. Un-bypass the Archetype X slot and check the tone again.'
Write-Host '   It must be the SAVED tone, not defaults. (Before the fix, the'
Write-Host '   stale marker made the app skip every state blob -> defaults.)'
Write-Host '5. Close BluePrinter normally.'
Wait-Key 'Press Enter when closed.'

Show-State

Write-Host ''
Write-Host '==================================================================='
Write-Host ' NOTES'
Write-Host '==================================================================='
Write-Host '- StateHash/StateLen of a slot change when its state is re-captured'
Write-Host '  (tweaked with the new build, or overwritten with defaults). If a'
Write-Host '  slot shows "(empty)" it was never saved and will load defaults.'
Write-Host '- If some blobs were already overwritten with defaults by the old'
Write-Host '  bug, compare StateHash here against these backup files:'
Write-Host '    %APPDATA%\Retrokielto\BluePrinter.properties.bak-corywong-20260902-122622'
Write-Host '    %APPDATA%\Retrokielto\BluePrinter.properties.bak-timhenson-20260902-123949'
Write-Host '    %APPDATA%\Retrokielto\BluePrinter.properties.bak-twinst-20260903-145241'
Write-Host '  and re-tweak, or restore the whole file (keeping libraryFolder).'