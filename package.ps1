# Builds the Release plugin, runs the tests and packages it into dist\:
#   SchematicAmpSim-<version>-win64.zip          the VST3 bundle (copy it to your VST3 folder)
#   SchematicAmpSim-<version>-win64-setup.exe    the installer, when Inno Setup 6 or 7 is installed
#   SHA256SUMS.txt                               checksums of both
# Usage: package.cmd [-SkipTests] [-SkipBuild]   (-SkipBuild: only package the plugin that is already in build\, as the CI does)
param([switch]$SkipTests, [switch]$SkipBuild)
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot

# The version is read from CMakeLists.txt (VERSION and SAS_VERSION_SUFFIX): the only place it is written.
$cmake = Get-Content CMakeLists.txt -Raw
$version = [regex]::Match($cmake, 'project\(SchematicAmpSim VERSION ([0-9]+\.[0-9]+\.[0-9]+)').Groups[1].Value
$suffix = [regex]::Match($cmake, 'set\(SAS_VERSION_SUFFIX "([^"]*)"\)').Groups[1].Value
if (-not $version) { throw 'could not read the version from CMakeLists.txt' }
$full = "$version$suffix"
Write-Host "Packaging Schematic Amp Sim $full"

if (-not $SkipBuild) {
    $build = Join-Path $PSScriptRoot "build.cmd"  # an explicit path: not every shell looks in the current folder
    if ($SkipTests) { & cmd /c "`"$build`"" } else { & cmd /c "`"$build`" test" }
    if ($LASTEXITCODE -ne 0) { throw 'the build or the tests failed: nothing was packaged' }
}

$bundle = 'build\plugin\SchematicAmpSim_artefacts\Release\VST3\Schematic Amp Sim.vst3'
if (-not (Test-Path $bundle)) { throw "the plugin bundle is missing: $bundle" }
New-Item -ItemType Directory -Force dist | Out-Null

$zip = "dist\SchematicAmpSim-$full-win64.zip"
if (Test-Path $zip) { Remove-Item $zip }
Compress-Archive -Path $bundle, 'LICENSE', 'README.md' -DestinationPath $zip
Write-Host "  $zip"

$roots = @($env:ProgramFiles, ${env:ProgramFiles(x86)}, "$env:LOCALAPPDATA\Programs")
$iscc = foreach ($v in 7, 6) { foreach ($r in $roots) { "$r\Inno Setup $v\ISCC.exe" } }
$iscc = $iscc | Where-Object { Test-Path $_ } | Select-Object -First 1
$files = @($zip)
if ($iscc) {
    & $iscc "/DAppVersion=$version" "/DAppVersionFull=$full" 'installer\schematic-amp-sim.iss'
    if ($LASTEXITCODE -ne 0) { throw 'Inno Setup failed' }
    $setup = "dist\SchematicAmpSim-$full-win64-setup.exe"
    Write-Host "  $setup"
    $files += $setup
} else {
    Write-Warning 'Inno Setup (6 or 7) is not installed: only the zip was built (https://jrsoftware.org/isinfo.php).'
}

# SHA-256 with .NET (Get-FileHash is not available in every PowerShell)
$sha = [System.Security.Cryptography.SHA256]::Create()
$lines = foreach ($f in $files) {
    $stream = [System.IO.File]::OpenRead((Resolve-Path $f))
    try { $hash = ([System.BitConverter]::ToString($sha.ComputeHash($stream)) -replace '-', '').ToLower() } finally { $stream.Dispose() }
    "$hash  $(Split-Path $f -Leaf)"
}
$lines | Set-Content dist\SHA256SUMS.txt
Write-Host 'Checksums: dist\SHA256SUMS.txt'
