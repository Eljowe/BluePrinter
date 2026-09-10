param(
    [string]$Config = "Release"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot

cmake --build (Join-Path $root "build") --config $Config --target BluePrinter_VST3
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$installScript = Join-Path $PSScriptRoot "install-vst3.ps1"
Start-Process powershell -Verb RunAs -Wait -ArgumentList @(
    "-NoProfile",
    "-ExecutionPolicy", "Bypass",
    "-File", "`"$installScript`""
)
