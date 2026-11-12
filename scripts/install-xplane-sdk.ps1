# Downloads the official X-Plane SDK and reports where it landed.
# AtmoForge needs 4.4.0 or newer: the browser window API is gated behind
# XPLM440.
param([string]$DepsDir = (Join-Path (Split-Path -Parent $PSScriptRoot) ".deps"))

$ErrorActionPreference = "Stop"

$version = "440-r1"
$url = "https://github.com/X-Plane/xpsdk/releases/download/xpsdk_4.4.0-r1/XPSDK440-r1.zip"
$sha256 = "73e6d8be3b0b3bf1fd703113e0b0df2aa854db753c66802d7379320dbc22f771"
$zip = Join-Path $DepsDir "XPSDK$version.zip"
$sdk = Join-Path $DepsDir "XPSDK$version"

New-Item -ItemType Directory -Force -Path $DepsDir | Out-Null
Write-Host "Downloading the official X-Plane SDK $version..."
Invoke-WebRequest -Uri $url -OutFile $zip
if ((Get-FileHash -Path $zip -Algorithm SHA256).Hash -ne $sha256) {
    throw "SDK download failed the SHA256 check (expected $sha256)."
}
if (Test-Path $sdk) { Remove-Item -Recurse -Force $sdk }
Expand-Archive -Path $zip -DestinationPath $sdk -Force

$header = Get-ChildItem -Path $sdk -Filter XPLMPlugin.h -Recurse | Select-Object -First 1
if (-not $header) { throw "SDK archive did not contain XPLMPlugin.h" }
$sdkRoot = $header.Directory.Parent.Parent.FullName
$env:XPLANE_SDK_PATH = $sdkRoot
Write-Host "SDK ready: $sdkRoot"
