param(
    [string]$Destination = "C:\Program Files\Common Files\VST3\BluePrinter.vst3"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$source = Join-Path $root "build\BluePrinter_artefacts\Release\VST3\BluePrinter.vst3"

if (-not (Test-Path -LiteralPath $source)) {
    Write-Host "VST3 bundle not found at $source - build Release first." -ForegroundColor Red
    exit 1
}

if (Test-Path -LiteralPath $Destination) {
    try {
        Remove-Item -LiteralPath $Destination -Recurse -Force
    }
    catch {
        Write-Host "Could not remove existing copy (is it loaded in a DAW?): $($_.Exception.Message)" -ForegroundColor Red
        exit 1
    }
}

Copy-Item -LiteralPath $source -Destination $Destination -Recurse -Force
Write-Host "Installed BluePrinter.vst3 -> $Destination" -ForegroundColor Green
