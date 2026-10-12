$ErrorActionPreference = 'Stop'
$sdk = Split-Path $PSScriptRoot -Parent
$root = Join-Path $sdk 'ThirdParty/HarfBuzz'
if (Test-Path $root) { throw 'HarfBuzz already exists; review it before replacing a vendored dependency.' }
$stage = Join-Path $sdk 'Intermediate/HarfBuzz-12.3.0'
New-Item -ItemType Directory -Force $stage | Out-Null
$archive = Join-Path $stage 'harfbuzz-12.3.0.tar.xz'
Invoke-WebRequest 'https://github.com/harfbuzz/harfbuzz/releases/download/12.3.0/harfbuzz-12.3.0.tar.xz' -OutFile $archive
if ((Get-FileHash $archive -Algorithm SHA256).Hash -ne '8660EBD3C27D9407FC8433B5D172BAFBA5F0317CB0BB4339F28E5370C93D42B7') { throw 'Unexpected HarfBuzz archive hash.' }
& tar -xf $archive -C $stage
if ($LASTEXITCODE -ne 0) { throw 'HarfBuzz extraction failed.' }
New-Item -ItemType Directory $root | Out-Null
Copy-Item -LiteralPath (Join-Path $stage 'harfbuzz-12.3.0/src') -Destination $root -Recurse
Copy-Item -LiteralPath (Join-Path $stage 'harfbuzz-12.3.0/COPYING') -Destination (Join-Path $root 'LICENSE')
Get-FileHash $archive -Algorithm SHA256
