$ErrorActionPreference = 'Stop'
$repo = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
Push-Location $repo
try {
    & .\BuildTool\bin\Beast.BuildTool.exe build Test/EcoSbor --platform Windows --config Debug
    if ($LASTEXITCODE -ne 0) { throw 'SDK build failed.' }
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    $vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vs) { throw 'MSVC not found.' }
    $vcvars = Join-Path $vs 'VC/Auxiliary/Build/vcvars64.bat'
    New-Item -ItemType Directory -Force Test/EcoSbor/Intermediate/Verification | Out-Null
    foreach ($source in @('Context', 'TextEditing', 'TextField', 'TextSegmentation')) {
        $command = '"{0}" >nul && cl /nologo /c /std:c++20 /utf-8 /EHsc /MDd /W4 /WX /I BeastSDK\Source BeastSDK\Source\UI\{1}.cpp /FoTest\EcoSbor\Intermediate\Verification\{1}.strict.obj' -f $vcvars, $source
        & cmd /c $command
        if ($LASTEXITCODE -ne 0) { throw "$source strict compilation failed." }
    }
    $command = '"{0}" >nul && cl /nologo /std:c++20 /utf-8 /EHsc /MDd /W4 /WX /I BeastSDK\Source BeastSDK\Tests\UITextFieldTests.cpp /FoTest\EcoSbor\Intermediate\Verification\UITextFieldTests.obj /FeTest\EcoSbor\Intermediate\Verification\UITextFieldTests.exe Test\EcoSbor\Intermediate\Build\BeastSDK\Windows\Debug\Beast.lib user32.lib gdi32.lib' -f $vcvars
    & cmd /c $command
    if ($LASTEXITCODE -ne 0) { throw 'TextField tests compilation failed.' }
    & .\Test\EcoSbor\Intermediate\Verification\UITextFieldTests.exe
    if ($LASTEXITCODE -ne 0) { throw 'TextField tests failed.' }
    $command = '"{0}" >nul && cl /nologo /std:c++20 /utf-8 /EHsc /MDd /W4 /WX /I BeastSDK\Source BeastSDK\Tests\UITextFeaturesTests.cpp /FoTest\EcoSbor\Intermediate\Verification\UITextFeaturesTests.obj /FeTest\EcoSbor\Intermediate\Verification\UITextFeaturesTests.exe Test\EcoSbor\Intermediate\Build\BeastSDK\Windows\Debug\Beast.lib user32.lib gdi32.lib' -f $vcvars
    & cmd /c $command
    if ($LASTEXITCODE -ne 0) { throw 'Text features tests compilation failed.' }
    & .\Test\EcoSbor\Intermediate\Verification\UITextFeaturesTests.exe
    if ($LASTEXITCODE -ne 0) { throw 'Text features tests failed.' }
    $command = '"{0}" >nul && cl /nologo /c /std:c++20 /utf-8 /EHsc /MDd /W4 /WX /I BeastSDK\Source BeastSDK\Source\Platform\Windows\WindowsAccessibility.cpp /FoTest\EcoSbor\Intermediate\Verification\WindowsAccessibility.strict.obj' -f $vcvars
    & cmd /c $command
    if ($LASTEXITCODE -ne 0) { throw 'Windows accessibility strict compilation failed.' }
    $command = '"{0}" >nul && cl /nologo /std:c++20 /utf-8 /EHsc /MDd /W4 /WX /I BeastSDK\Source BeastSDK\Tests\UIAccessibilityTests.cpp /FoTest\EcoSbor\Intermediate\Verification\UIAccessibilityTests.obj /FeTest\EcoSbor\Intermediate\Verification\UIAccessibilityTests.exe Test\EcoSbor\Intermediate\Build\BeastSDK\Windows\Debug\Beast.lib user32.lib gdi32.lib' -f $vcvars
    & cmd /c $command
    if ($LASTEXITCODE -ne 0) { throw 'Accessibility tests compilation failed.' }
    & .\Test\EcoSbor\Intermediate\Verification\UIAccessibilityTests.exe
    if ($LASTEXITCODE -ne 0) { throw 'Accessibility tests failed.' }
} finally { Pop-Location }
