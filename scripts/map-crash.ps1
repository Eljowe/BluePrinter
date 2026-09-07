# Map BluePrinter crash-info.txt module+offset backtraces to function names
# from the linker .map file (Release builds emit BluePrinter.map next to the
# exe; see CMakeLists.txt). Plain text, no WinDbg/DIA required.
#
# Usage:
#   powershell -NoProfile -ExecutionPolicy Bypass -File scripts\map-crash.ps1
#       -Map  build\BluePrinter_artefacts\Release\Standalone\BluePrinter.map
#       -CrashInfo "%APPDATA%\Retrokielto\crash-info.txt"
#   (or -Offset @(0xec67e, 0x229948) instead of -CrashInfo).
#
# Prints one line per frame:  0x<RVA> -> <symbol>+0x<disp>  (object)

param(
    [Parameter(Mandatory=$true)][string]$Map,
    [string]$CrashInfo,
    [long[]]$Offset
)

$ErrorActionPreference = 'Stop'

function Get-VirtualAddress {
    param([string]$Hex, [long]$Base)
    $v = [Convert]::ToInt64("0x$($Hex.Trim())", 16)  # ToInt64 with 0x prefix fails; use Convert with radix
    return 0
}

$lines = Get-Content (Resolve-Path $Map)

$base = 0x140000000L
foreach ($l in $lines) {
    if ($l -match '^  Preferred load address is ([0-9A-Fa-f]+)') {
        $base = [Convert]::ToInt64($Matches[1], 16)
        break
    }
}

# Entries: " 0001:00001234  SymbolName  1400EC660  f  proj.obj"
$entries = [System.Collections.Generic.List[object]]::new()
foreach ($l in $lines) {
    if ($l -match '^ ([0-9A-Fa-f]{4}):([0-9A-Fa-f]{8})\s+([^ ]+)\s+([0-9A-Fa-f]{8,16})\s+(f|i)\s+') {
        $vaHex = $Matches[4]
        $va = [Convert]::ToInt64($vaHex, 16)
        $rva = if ($va -ge $base) { $va - $base } else { $va }
        $entries.Add([pscustomobject]@{ Rva = $rva; Name = $Matches[3]; Obj = $Matches[5] })
    }
}
Write-Host "loaded $($entries.Count) map entries (image base 0x$($base.ToString('X')))"

$offsets = @()
if ($CrashInfo -and (Test-Path $CrashInfo)) {
    $text = Get-Content -Raw (Resolve-Path $CrashInfo)
    if ($text -match 'Fault offset: 0x([0-9a-fA-F]+)') {
        $offsets += [Convert]::ToInt64($Matches[1], 16)
    }
    foreach ($line in ($text -split "`r?`n")) {
        if ($line -match 'BluePrinter\.exe \+ 0x([0-9a-fA-F]+)') {
            $offsets += [Convert]::ToInt64($Matches[1], 16)
        }
    }
} elseif ($Offset) {
    $offsets = $Offset
} else {
    throw 'Specify -CrashInfo or -Offset'
}

# Sort by Rva once for binary-search-lite lookups.
$sorted = @($entries | Sort-Object Rva)
foreach ($off in ($offsets | Select-Object -Unique)) {
    $best = $null
    foreach ($e in $sorted) {
        if ($e.Rva -le $off) { $best = $e } else { break }
    }
    if ($null -ne $best) {
        Write-Host ("0x{0:X} -> {1}+0x{2:X}   ({3})" -f $off, $best.Name, ($off - $best.Rva), $best.Obj)
    } else {
        Write-Host ("0x{0:X} -> (no symbol <= rva)" -f $off)
    }
}