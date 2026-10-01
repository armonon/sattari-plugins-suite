param(
    [ValidateSet('CurrentUser', 'AllUsers')][string]$Scope = 'CurrentUser',
    [string]$PackageRoot = $PSScriptRoot
)
$ErrorActionPreference = 'Stop'
$manifest = Get-Content (Join-Path $PackageRoot 'manifest.json') -Raw | ConvertFrom-Json
if ($manifest.platform -ne 'windows-x64') { throw 'This installer requires the Windows x64 package.' }
foreach ($file in $manifest.files) {
    $path = Join-Path $PackageRoot $file.path
    if (-not (Test-Path $path -PathType Leaf)) { throw "Missing file: $($file.path)" }
    if ((Get-FileHash $path -Algorithm SHA256).Hash.ToLowerInvariant() -ne $file.sha256) {
        throw "Checksum mismatch: $($file.path)"
    }
}
if ($Scope -eq 'AllUsers') {
    $principal = [Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw 'AllUsers installation requires an elevated PowerShell window.'
    }
    $vst = Join-Path $env:CommonProgramFiles 'VST3'
    $apps = Join-Path $env:ProgramFiles 'Sattari'
} else {
    $vst = Join-Path $env:LOCALAPPDATA 'Programs/Common/VST3'
    $apps = Join-Path $env:LOCALAPPDATA 'Programs/Sattari'
}
$backup = Join-Path $env:LOCALAPPDATA ('Sattari/InstallBackups/' + [guid]::NewGuid().ToString())
foreach ($folder in @($vst, $apps, $backup)) { New-Item -ItemType Directory -Force $folder | Out-Null }
foreach ($product in $manifest.products) {
    $bundle = "$($product.name).vst3"
    $destination = Join-Path $vst $bundle
    if (Test-Path $destination) { Move-Item $destination (Join-Path $backup $bundle) }
    Copy-Item (Join-Path $PackageRoot "VST3/$bundle") $destination -Recurse
    if ($product.standalone) {
        $exe = "$($product.name).exe"
        $destination = Join-Path $apps $exe
        if (Test-Path $destination) { Move-Item $destination (Join-Path $backup $exe) }
        Copy-Item (Join-Path $PackageRoot "Standalone/$exe") $destination
    }
}
Write-Host "Installed five Sattari candidates. VST3: $vst"
Write-Host "Standalone apps: $apps"
Write-Host "Previous files: $backup"
Write-Host 'Restart/rescan your DAW. These are unsigned internal test builds.'
