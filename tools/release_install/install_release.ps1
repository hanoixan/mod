# Installs an already-built mod release from GitHub on this Windows machine, for this user:
#   powershell -ExecutionPolicy Bypass -File tools\release_install\install_release.ps1 -Version 1.2.0 [-Prefix DIR]
# -Version is the release's version as tagged, without the v: 1.2.0, or a candidate, 1.2.0-rc.1.
# Unpacks the release's zip into -Prefix (default %LOCALAPPDATA%\Programs\mod) and adds its bin
# folder to the user PATH. There is no system install on Windows. install.ps1 at the
# repository's root, the README's one-liner, is the installer for users; this one is for
# developers, and never picks the version by itself.
param(
    [Parameter(Mandatory = $true)][string]$Version,
    [string]$Prefix = (Join-Path $env:LOCALAPPDATA 'Programs\mod')
)
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'  # Invoke-WebRequest is far slower with its progress bar

function Say([string]$message) { Write-Host "install_release: $message" }
function Die([string]$message) { [Console]::Error.WriteLine("install_release: $message"); exit 1 }

if ($Version -notmatch '^\d+\.\d+\.\d+(-rc\.\d+)?$') {
    Die "not a release version: $Version (want one like 1.2.0 or 1.2.0-rc.1)"
}
$releases = if ($env:MOD_RELEASES) { $env:MOD_RELEASES } else { 'https://github.com/hanoixan/mod/releases' }
$plain = $Version -replace '-rc\.\d+$', ''  # the files in a release carry the plain version
$name = "mod-$plain-windows-x86_64"

$tmp = Join-Path ([IO.Path]::GetTempPath()) ("mod-install-release-" + [guid]::NewGuid())
New-Item -ItemType Directory -Path $tmp | Out-Null
try {
    $zip = Join-Path $tmp "$name.zip"
    $url = "$releases/download/v$Version/$name.zip"
    Say "downloading $url"
    try {
        Invoke-WebRequest -Uri $url -OutFile $zip -UseBasicParsing
    } catch {
        Die "cannot download $url (is $Version released, with that file?): $($_.Exception.Message)"
    }
    Expand-Archive -Path $zip -DestinationPath $tmp
    New-Item -ItemType Directory -Force -Path $Prefix | Out-Null
    Copy-Item -Recurse -Force -Path (Join-Path $tmp "$name\*") -Destination $Prefix
} finally {
    Remove-Item -Recurse -Force -Path $tmp -ErrorAction SilentlyContinue
}

# The installed mod must say the version asked for.
$bin = Join-Path $Prefix 'bin'
$exe = Join-Path $bin 'mod.exe'
$got = (& $exe --version 2>&1 | Out-String).Trim()
if ($LASTEXITCODE -ne 0 -or $got -notmatch " $([regex]::Escape($plain))\b") {
    Die "$exe says `"$got`", not $plain"
}
Say "installed ${exe}: $got"

$userPath = [Environment]::GetEnvironmentVariable('Path', 'User')
$entries = if ($userPath) { $userPath -split ';' | Where-Object { $_ } } else { @() }
if ($entries -notcontains $bin) {
    [Environment]::SetEnvironmentVariable('Path', (($entries + $bin) -join ';'), 'User')
    Say "added $bin to your user PATH; open a new terminal to run mod"
}
