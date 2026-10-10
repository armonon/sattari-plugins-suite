$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$version = '1.22.0'
$destination = Join-Path $repo "third_party/onnxruntime-win-x64-$version"
$temporary = if ($env:RUNNER_TEMP) { $env:RUNNER_TEMP } else { $env:TEMP }
$archive = Join-Path $temporary "onnxruntime-win-x64-$version.zip"
$expected = '174c616efc0271194488642a72f1a514e01487da4dfe84c49296d66e40ebe0da'
Invoke-WebRequest "https://github.com/microsoft/onnxruntime/releases/download/v$version/onnxruntime-win-x64-$version.zip" -OutFile $archive
if ((Get-FileHash $archive -Algorithm SHA256).Hash.ToLowerInvariant() -ne $expected) {
    throw 'Pinned ONNX Runtime archive checksum mismatch'
}
if (Test-Path $destination) { throw "Refusing to overwrite existing SDK: $destination" }
Expand-Archive $archive (Join-Path $repo 'third_party')
if (-not (Test-Path "$destination/lib/onnxruntime.dll") -or -not (Test-Path "$destination/lib/onnxruntime.lib")) {
    throw 'Windows ONNX Runtime SDK is incomplete'
}
Write-Output $destination
