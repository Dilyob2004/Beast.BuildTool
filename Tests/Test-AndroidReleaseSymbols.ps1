param(
    [string]$Project = 'Test/EcoSbor',
    [Parameter(Mandatory = $true)]
    [string]$Ndk
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem
$projectRoot = (Resolve-Path -LiteralPath $Project).Path
$name = Split-Path $projectRoot -Leaf
$output = Join-Path $projectRoot 'Binaries/Android/Release'
$symbols = Join-Path $output 'Symbols/arm64-v8a'
$manifest = Get-Content (Join-Path $symbols 'manifest.json') -Raw | ConvertFrom-Json
$apk = Join-Path $output "$name.apk"
if ((Get-FileHash $apk).Hash -ne $manifest.apkSha256) { throw 'APK/manifest mismatch' }
$readelf = Join-Path $Ndk 'toolchains/llvm/prebuilt/windows-x86_64/bin/llvm-readelf.exe'
function Read-Elf([string]$Option, [string]$FilePath) {
    $result = & $readelf $Option --wide $FilePath
    if ($LASTEXITCODE -ne 0) { throw "readelf failed: $FilePath" }
    return ($result -join "`n")
}
$zip = [IO.Compression.ZipFile]::OpenRead($apk)
try {
    foreach ($record in $manifest.libraries) {
        $original = Join-Path $symbols $record.library
        $staged = Join-Path $projectRoot "Platforms/Android/app/src/main/jniLibs/arm64-v8a/$($record.library)"
        if ((Get-FileHash $original).Hash -ne $record.originalSha256) { throw 'Original hash mismatch' }
        if ((Get-FileHash $staged).Hash -ne $record.packagedSha256) { throw 'Staging hash mismatch' }
        $entry = $zip.GetEntry("lib/arm64-v8a/$($record.library)")
        if (-not $entry) { throw 'APK library missing' }
        $stream = $entry.Open()
        try { $hash = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($stream)) }
        finally { $stream.Dispose() }
        if ($hash -ne $record.packagedSha256) { throw 'APK library differs from stripped file' }
        if ((Read-Elf '--sections' $original) -notmatch '\.debug_info') { throw 'Original lacks DWARF debug info' }
        if ((Read-Elf '--sections' $staged) -match '\.debug_|\s\.symtab\s') { throw 'Deployment still contains debug/static symbols' }
        if ((Read-Elf '--dyn-syms' $original) -ne (Read-Elf '--dyn-syms' $staged)) { throw 'Dynamic symbols changed' }
        $before = [regex]::Match((Read-Elf '--notes' $original), 'Build ID: (\w+)').Groups[1].Value
        $after = [regex]::Match((Read-Elf '--notes' $staged), 'Build ID: (\w+)').Groups[1].Value
        if (-not $before -or $before -ne $after) { throw 'Build ID mismatch' }
        if ((Get-Item $staged).Length -ge (Get-Item $original).Length) { throw 'No size reduction' }
        Write-Host "$($record.library): hashes, DWARF removal, dynamic symbols and build ID verified."
    }
} finally { $zip.Dispose() }
Write-Host "Release APK verified: $((Get-Item $apk).Length) bytes."
