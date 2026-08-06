# Builds the full release bundle:
#   build\release\BluePrinter-<version>\
#     BluePrinterSetup-<version>.exe   (installer)
#     LICENSE
#     README.md
#     SHA256SUMS.txt                   (checksums of the above)
#
# Version comes from CMakeLists.txt (project VERSION) and is passed to
# Inno Setup via /D, so there is a single source of truth.

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot

# 1. Version from CMakeLists.txt
$cmakeLists = Get-Content (Join-Path $root "CMakeLists.txt") -Raw
if ($cmakeLists -notmatch "project\(BluePrinter VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)") {
    throw "Could not parse version from CMakeLists.txt"
}
$version = $Matches[1]
Write-Host "Building BluePrinter $version release bundle..." -ForegroundColor Cyan

# 2. WebUI
Push-Location (Join-Path $root "WebUI")
try { npm run build } finally { Pop-Location }

# 3. C++ Release targets
cmake --build (Join-Path $root "build") --config Release --target BluePrinter_Standalone BluePrinter_VST3
if ($LASTEXITCODE -ne 0) { throw "C++ build failed" }

# 4. Installer (version override keeps CMakeLists as the single source)
$iscc = Join-Path $env:LOCALAPPDATA "Programs\Inno Setup 6\ISCC.exe"
if (-not (Test-Path $iscc)) { $iscc = "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" }
if (-not (Test-Path $iscc)) { throw "Inno Setup not found - install via: winget install JRSoftware.InnoSetup" }
& $iscc "/DMyAppVersion=$version" (Join-Path $root "installer\BluePrinter.iss")
if ($LASTEXITCODE -ne 0) { throw "Inno Setup compile failed" }

# 5. Assemble the release folder
$releaseDir = Join-Path $root "build\release\BluePrinter-$version"
New-Item -ItemType Directory -Path $releaseDir -Force | Out-Null
Copy-Item (Join-Path $root "build\installer\BluePrinterSetup-$version.exe") $releaseDir
Copy-Item (Join-Path $root "LICENSE") $releaseDir
Copy-Item (Join-Path $root "README.md") $releaseDir

# 6. Checksums
$hashes = foreach ($f in Get-ChildItem $releaseDir -File) {
    "{0}  {1}" -f (Get-FileHash $f.FullName -Algorithm SHA256).Hash.ToLower(), $f.Name
}
$hashes | Out-File (Join-Path $releaseDir "SHA256SUMS.txt") -Encoding ascii

Write-Host "" -ForegroundColor Cyan
Write-Host "Release bundle: $releaseDir" -ForegroundColor Green
Get-ChildItem $releaseDir | ForEach-Object {
    $mb = [math]::Round($_.Length / 1MB, 1)
    Write-Host ("  {0}  ({1} MB)" -f $_.Name, $mb)
}
