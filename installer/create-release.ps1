# Creates a GitHub release for the bundle produced by build-release.ps1
# and uploads every file in it (installer, LICENSE, README, SHA256SUMS) as
# an asset. Draft by default; pass -Published to publish immediately.
#
# Needs a GitHub token with "Contents: read and write" (fine-grained PAT)
# or the classic "repo" scope. Provide it either via $env:GH_TOKEN, the
# -Token parameter, or a file at "$HOME\.blueprinter-gh-token" (readable
# from both Windows and WSL as /mnt/c/Users/<you>/.blueprinter-gh-token):
#   $env:GH_TOKEN = "ghp_..."
# No gh CLI or local git credentials are required - everything goes
# through the REST API (including tag creation).

param(
    [string]$Token = $env:GH_TOKEN,
    [switch]$Published
)

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot

$tokenFile = Join-Path $HOME ".blueprinter-gh-token"
if ([string]::IsNullOrWhiteSpace($Token) -and (Test-Path -LiteralPath $tokenFile)) {
    $Token = (Get-Content -LiteralPath $tokenFile -Raw).Trim()
}

if ([string]::IsNullOrWhiteSpace($Token)) {
    throw "No GitHub token. Set env var GH_TOKEN, pass -Token, or put it in $tokenFile (classic 'repo' or fine-grained 'Contents: read and write' scope)."
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
        $existingRef = Invoke-RestMethod -Method Get -Headers $headers -Uri "$api/repos/$repo/git/ref/tags/$tag"
        if ($existingRef.object.sha -ne $branchInfo.commit.sha) {
            throw "Tag $tag already exists but points at $($existingRef.object.sha), not the $defaultBranch head ($($branchInfo.commit.sha)). Delete the tag or bump the version in CMakeLists.txt."
        }
        Write-Host "Tag $tag already exists at the $defaultBranch head"
    }
    else { throw }
}

# 2. Refuse to create a duplicate release for this tag
$allReleases = Invoke-RestMethod -Method Get -Headers $headers -Uri "$api/repos/$repo/releases?per_page=100"
if ($allReleases | Where-Object { $_.tag_name -eq $tag }) {
    throw "A release for $tag already exists. Publish or delete it (or bump the version in CMakeLists.txt) before creating another."
}

# 3. Create the release
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

# 4. Upload every file in the bundle as an asset
$uploadBase = $release.upload_url -replace "\{\?name,label\}", ""
foreach ($file in Get-ChildItem $releaseDir -File) {
    Invoke-RestMethod -Method Post -Headers $headers `
        -Uri ($uploadBase + "?name=" + [uri]::EscapeDataString($file.Name)) `
        -ContentType "application/octet-stream" -InFile $file.FullName | Out-Null
    Write-Host ("  uploaded " + $file.Name)
}

Write-Host "Done. Review and publish: $($release.html_url)" -ForegroundColor Green
