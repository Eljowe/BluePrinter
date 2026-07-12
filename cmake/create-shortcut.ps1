param(
    [Parameter(Mandatory = $true)]
    [string]$ShortcutPath,

    [Parameter(Mandatory = $true)]
    [string]$TargetPath,

    [string]$WorkingDirectory = "",
    [string]$Description = "BluePrinter"
)

# Create or overwrite a .lnk file. WScript.Shell.CreateShortcut is a
# built-in Windows API; it overwrites the target file if it already
# exists, so re-running install is safe.
$shell = New-Object -ComObject WScript.Shell
$shortcut = $shell.CreateShortcut($ShortcutPath)
$shortcut.TargetPath = $TargetPath
if ($WorkingDirectory -ne "") { $shortcut.WorkingDirectory = $WorkingDirectory }
$shortcut.WindowStyle = 1   # normal window
$shortcut.Description = $Description
$shortcut.Save()

if (-not (Test-Path $ShortcutPath)) {
    Write-Error "Failed to create shortcut at $ShortcutPath"
    exit 1
}
