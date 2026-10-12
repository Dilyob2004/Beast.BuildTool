$ErrorActionPreference = 'Stop'
$sdk = Split-Path $PSScriptRoot -Parent
$target = Join-Path $sdk 'ThirdParty/SheenBidi'
if (Test-Path $target) { throw 'Review existing SheenBidi before replacing it.' }
$stage = Join-Path $sdk 'Intermediate/SheenBidi-3.0.0'
New-Item -ItemType Directory -Force $stage | Out-Null
$archive = Join-Path $stage 'SheenBidi.tar.gz'
Invoke-WebRequest 'https://github.com/Tehreer/SheenBidi/archive/refs/tags/v3.0.0.tar.gz' -OutFile $archive
if ((Get-FileHash $archive -Algorithm SHA256).Hash -ne '86C56014034739BA39A24C23EB00323B0BF6F737354F665786015FCA842AF786') { throw 'SheenBidi archive hash mismatch.' }
& tar -xf $archive -C $stage
if ($LASTEXITCODE -ne 0) { throw 'Extraction failed.' }
New-Item -ItemType Directory $target | Out-Null
foreach ($name in @('Headers', 'Source', 'LICENSE')) { Copy-Item -LiteralPath (Join-Path $stage "SheenBidi-3.0.0/$name") -Destination $target -Recurse }
Get-FileHash $archive -Algorithm SHA256
