$ErrorActionPreference = 'Stop'
$repo = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
Push-Location $repo
try {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    $vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vs) { throw 'MSVC not found.' }
    $vcvars = Join-Path $vs 'VC/Auxiliary/Build/vcvars64.bat'
    New-Item -ItemType Directory -Force Test/EcoSbor/Intermediate/Verification/Unicode | Out-Null
    $command = '"{0}" >nul && cl /nologo /std:c++20 /utf-8 /EHsc /W4 /WX /I BeastSDK\Source BeastSDK\Tests\UnicodeTests.cpp BeastSDK\Source\UI\TextEditing.cpp BeastSDK\Source\UI\TextSegmentation.cpp /FoTest\EcoSbor\Intermediate\Verification\Unicode\ /FeTest\EcoSbor\Intermediate\Verification\Unicode\UnicodeTests.exe' -f $vcvars
    & cmd /c $command
    if ($LASTEXITCODE -ne 0) { throw 'Unicode tests compilation failed.' }
    & ./Test/EcoSbor/Intermediate/Verification/Unicode/UnicodeTests.exe
    if ($LASTEXITCODE -ne 0) { throw 'Unicode tests failed.' }
} finally { Pop-Location }
