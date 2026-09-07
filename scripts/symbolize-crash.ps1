# Map BluePrinter crash-info.txt module+offset backtraces to function names
# using the matching executable + PDB via dbghelp (SymFromAddr).
#
# Usage:
#   powershell -NoProfile -ExecutionPolicy Bypass -File scripts\symbolize-crash.ps1
#       -Exe  build\BluePrinter_artefacts\Release\Standalone\BluePrinter.exe
#       -CrashInfo "%APPDATA%\Retrokielto\crash-info.txt"
#   (omit -CrashInfo to pass -Offset @(0xec67e,0x229948) explicitly.)
#
# The exe's PDB must sit next to it (Release builds now emit one, see
# CMakeLists.txt). Prints one line per offset:
#   0xEC67E -> BluePrinterAudioProcessor::something+0x0

param(
    [Parameter(Mandatory=$true)][string]$Exe,
    [string]$CrashInfo,
    [long[]]$Offset
)

$ErrorActionPreference = 'Stop'

$csharp = @'
using System;
using System.Runtime.InteropServices;

[StructLayout(LayoutKind.Sequential)]
public struct SYMBOL_INFO
{
    public uint SizeOfStruct;                    // 0
    public uint TypeIndex;                       // 4
    public ulong Value;                          // 8
    public uint Address;                         // 16
    public uint Flags;                           // 20
    public uint Reserved;                        // 24
    public uint Tag;                             // 28
    public uint Size;                            // 32
    public uint MaxNameLen;                      // 36
    [MarshalAs(UnmanagedType.ByValArray, SizeConst = 1024)]
    public byte[] Name;                          // 40 ..
}

public static class Dbg
{
    [DllImport("dbghelp.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    public static extern bool SymInitialize(IntPtr hProcess, string UserSearchPath, bool fInvadeProcess);

    [DllImport("dbghelp.dll", SetLastError = true)]
    public static extern bool SymCleanup(IntPtr hProcess);

    [DllImport("dbghelp.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    public static extern ulong SymLoadModuleEx(IntPtr hProcess, IntPtr hFile, string ImageName,
        string ModuleName, ulong BaseOfDll, uint DllSize, IntPtr Data, uint Flags);

    [DllImport("dbghelp.dll", SetLastError = true)]
    public static extern uint SymSetOptions(uint SymOptions);

    [DllImport("dbghelp.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    public static extern bool SymFromAddr(IntPtr hProcess, ulong Address, out ulong Displacement,
        ref SYMBOL_INFO Symbol);

    [DllImport("kernel32.dll")]
    public static extern IntPtr GetCurrentProcess();
}
'@

Add-Type -TypeDefinition $csharp

$h = [Dbg]::GetCurrentProcess()
# SYMOPT_UNDNAME(2) | SYMOPT_DEFERRED_LOADS(4) | SYMOPT_LOAD_LINES(16)
[Dbg]::SymSetOptions(2 -bor 4 -bor 16) | Out-Null
if (-not [Dbg]::SymInitialize($h, $null, $true)) {
    throw "SymInitialize failed (Win32 $([Runtime.InteropServices.Marshal]::GetLastWin32Error()))"
}

try {
    $exeFull = (Resolve-Path $Exe).Path
    $mod = [Dbg]::SymLoadModuleEx($h, [IntPtr]::Zero, $exeFull, $null, [uint64]0x140000000, 0, [IntPtr]::Zero, 0)
    if ($mod -eq 0) { throw "SymLoadModuleEx failed (Win32 $([Runtime.InteropServices.Marshal]::GetLastWin32Error()))" }

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

    $offsets = @($offsets | Select-Object -Unique)

    foreach ($off in $offsets) {
        $sym = New-Object SYMBOL_INFO
        $sym.SizeOfStruct = [Runtime.InteropServices.Marshal]::SizeOf([type][SYMBOL_INFO])
        $sym.MaxNameLen = 1024
        $sym.Name = New-Object byte[] 1024
        $disp = [uint64]0
        $ok = [Dbg]::SymFromAddr($h, [uint64]0x140000000 + [uint64]$off, [ref]$disp, [ref]$sym)
        if ($ok) {
            $name = [System.Text.Encoding]::ASCII.GetString($sym.Name).Trim([char]0).Trim()
            Write-Host ("0x{0:X} -> {1}+0x{2:X}" -f $off, $name, $disp)
        } else {
            Write-Host ("0x{0:X} -> (no symbol, Win32 {1})" -f $off, [Runtime.InteropServices.Marshal]::GetLastWin32Error())
        }
    }
} finally {
    [Dbg]::SymCleanup($h) | Out-Null
}