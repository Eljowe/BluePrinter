# Creates a GitHub release for the bundle produced by build-release.ps1
# and uploads every file in it (installer, LICENSE, README, SHA256SUMS) as
# an asset. Draft by default; pass -Published to publish immediately.
#
# Needs a GitHub token with "Contents: read and write" (fine-grained PAT)
# or the classic "repo" scope:
#   $env:GH_TOKEN = "ghp_..."
# No gh CLI or local git credentials are required - everything goes
# through the REST API (including tag creation).

param(
    [string]$Token = $env:GH_TOKEN,
    [switch]$Published
)

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot

if ([string]::IsNullOrWhiteSpace($Token)) {
    throw "No GitHub token. Set env var GH_TOKEN (classic 'repo' or fine-grained 'Contents: read and write' scope)."
}

$cmakeLists = Get-Content (Join-Path $root "CMakeLists.txt") -Raw
if ($cmakeLists -notmatch "project\(BluePrinter VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)") {
    throw "Could not parse version from CMakeLists.txt"
}
$version = $Matches[1]
$tag = "v$version"

$releaseDir = Join-Path $root "build\release\BluePrinter-$version"
if (-not (Test-Path $releaseDir)) {
    throw "Release bundle not found at $releaseDir - run installer\build-release.ps1 first."
}

$headers = @{
    Authorization = "Bearer $Token"
    Accept        = "application/vnd.github+json"
}

$api = "https://api.github.com"
$repo = "Eljowe/BluePrinter"
$draft = -not $Published

# 1. Tag the default-branch head (lightweight), unless it already exists
$repoInfo = Invoke-RestMethod -Method Get -Headers $headers -Uri "$api/repos/$repo"
$defaultBranch = $repoInfo.default_branch
$branchInfo = Invoke-RestMethod -Method Get -Headers $headers -Uri "$api/repos/$repo/branches/$defaultBranch"
try {
    Invoke-RestMethod -Method Post -Headers $headers -Uri "$api/repos/$repo/git/refs" `
        -ContentType "application/json" `
        -Body (@{ ref = "refs/tags/$tag"; sha = $branchInfo.commit.sha } | ConvertTo-Json) | Out-Null
    Write-Host "Created tag $tag at $defaultBranch head"
}
catch {
    if ($_.Exception.Response.StatusCode.value__ -eq 422) {
        Write-Host "Tag $tag already exists"
    }
    else { throw }
}

# 2. Create the release
$releaseBody = @{
    tag_name         = $tag
    target_commitish = $defaultBranch
    name             = "BluePrinter $version"
    body             = "Installer bundle - see the README's Install section for what it contains."
    draft            = $draft
} | ConvertTo-Json
$release = Invoke-RestMethod -Method Post -Headers $headers -Uri "$api/repos/$repo/releases" `
    -ContentType "application/json" -Body $releaseBody
Write-Host ("Created release: " + $release.html_url + " (draft: " + $draft + ")")

# 3. Upload every file in the bundle as an asset
$uploadBase = $release.upload_url -replace "\{\?name,label\}", ""
foreach ($file in Get-ChildItem $releaseDir -File) {
    Invoke-RestMethod -Method Post -Headers $headers `
        -Uri ($uploadBase + "?name=" + [uri]::EscapeDataString($file.Name)) `
        -ContentType "application/octet-stream" -InFile $file.FullName | Out-Null
    Write-Host ("  uploaded " + $file.Name)
}

Write-Host "Done. Review and publish: $($release.html_url)" -ForegroundColor Green
