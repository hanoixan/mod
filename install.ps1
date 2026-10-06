# Installs mod on Windows:
#   irm https://raw.githubusercontent.com/hanoixan/mod/main/install.ps1 | iex
# Unpacks the release's zip into $env:MOD_PREFIX (default: %LOCALAPPDATA%\Programs\mod) and
# adds its bin folder to your user PATH. $env:MOD_VERSION picks a release (default: the
# latest), or a release candidate such as 1.2.0-rc.1.
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'  # Invoke-WebRequest is far slower with its progress bar

$releases = if ($env:MOD_RELEASES) { $env:MOD_RELEASES } else { 'https://github.com/hanoixan/mod/releases' }
$api = if ($env:MOD_API) { $env:MOD_API } else { 'https://api.github.com/repos/hanoixan/mod/releases/latest' }
$prefix = if ($env:MOD_PREFIX) { $env:MOD_PREFIX } else { Join-Path $env:LOCALAPPDATA 'Programs\mod' }

function Say([string]$message) { Write-Host "mod install: $message" }

# `release` names the tag (v1.2.0, or v1.2.0-rc.1 for a candidate); `version`, the files in it.
$release = $env:MOD_VERSION
if (-not $release) {
    $release = (Invoke-RestMethod -Uri $api).tag_name -replace '^v', ''
    if (-not $release) { throw "mod install: no release found at $api" }
}
$version = $release -replace '-rc\.\d+$', ''
$name = "mod-$version-windows-x86_64"
Say "installing mod $release"

$tmp = Join-Path ([IO.Path]::GetTempPath()) ("mod-install-" + [guid]::NewGuid())
New-Item -ItemType Directory -Path $tmp | Out-Null
try {
    $zip = Join-Path $tmp "$name.zip"
    $url = "$releases/download/v$release/$name.zip"
    Say "downloading $name.zip"
    Invoke-WebRequest -Uri $url -OutFile $zip -UseBasicParsing
    Expand-Archive -Path $zip -DestinationPath $tmp
    New-Item -ItemType Directory -Force -Path $prefix | Out-Null
    Copy-Item -Recurse -Force -Path (Join-Path $tmp "$name\*") -Destination $prefix
} finally {
    Remove-Item -Recurse -Force -Path $tmp -ErrorAction SilentlyContinue
}

$bin = Join-Path $prefix 'bin'
Say "installed $bin\mod.exe"
$userPath = [Environment]::GetEnvironmentVariable('Path', 'User')
$entries = if ($userPath) { $userPath -split ';' | Where-Object { $_ } } else { @() }
if ($entries -notcontains $bin) {
    [Environment]::SetEnvironmentVariable('Path', (($entries + $bin) -join ';'), 'User')
    Say "added $bin to your user PATH; open a new terminal to run mod"
}
