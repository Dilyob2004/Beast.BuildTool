$ErrorActionPreference = 'Stop'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'MSVC not found.' }
$vcvars = Join-Path $vs 'VC/Auxiliary/Build/vcvars64.bat'
New-Item -ItemType Directory -Force Test/EcoSbor/Intermediate/Verification | Out-Null
foreach ($source in @('BeastSDK\Source\UI\UIApplication.cpp','BeastSDK\Source\UI\Widget.cpp','BeastSDK\Source\UI\RichWidgets.cpp','BeastSDK\Source\UI\Context.cpp','BeastSDK\Source\UI\Layout.cpp','BeastSDK\Source\UI\Private\Canvas.cpp','BeastSDK\Source\UI\Private\CanvasLayers.cpp','BeastSDK\Source\UI\Private\RepaintBoundary.cpp','BeastSDK\Source\UI\Private\LayerWidgets.cpp','BeastSDK\Source\UI\Private\Font.cpp','BeastSDK\Source\UI\Private\GlyphAtlas.cpp','Test\EcoSbor\Source\Launch\EcoScene.cpp')) {
    $object = [System.IO.Path]::GetFileNameWithoutExtension($source)
    $command = '"{0}" >nul && cl /nologo /c /std:c++20 /utf-8 /EHsc /MDd /W4 /WX /I BeastSDK\Source /I Test\EcoSbor\Source "{1}" /FoTest\EcoSbor\Intermediate\Verification\{2}.obj' -f $vcvars, $source, $object
    & cmd /c $command
    if ($LASTEXITCODE -ne 0) { throw "$source strict compilation failed." }
}
$command = '"{0}" >nul && cl /nologo /std:c++20 /utf-8 /EHsc /MDd /W4 /WX /I BeastSDK\Source /I BeastSDK\ThirdParty\OpenGL /I Test\EcoSbor\Source Test\EcoSbor\Tests\EcoTests.cpp Test\EcoSbor\Intermediate\Verification\EcoScene.obj /FoTest\EcoSbor\Intermediate\Verification\EcoTests.obj /FeTest\EcoSbor\Intermediate\Verification\EcoTests.exe Test\EcoSbor\Intermediate\Build\BeastSDK\Windows\Debug\Beast.lib user32.lib gdi32.lib' -f $vcvars
& cmd /c $command
if ($LASTEXITCODE -ne 0) { throw 'EcoTests compilation failed.' }
& ./Test/EcoSbor/Intermediate/Verification/EcoTests.exe 2>&1 | Tee-Object -FilePath Test/EcoSbor/Intermediate/Verification/EcoTests.log
if ($LASTEXITCODE -ne 0) { throw 'EcoTests failed.' }
foreach ($source in @('BeastSDK\Tests\UIScrollHostTests.cpp', 'BeastSDK\Tests\UITextMetricsTests.cpp', 'BeastSDK\Tests\UIFontFallbackTests.cpp', 'BeastSDK\Tests\UIGlyphPrefetchTests.cpp', 'Test\EcoSbor\Tests\ReloadStateTests.cpp', 'Test\EcoSbor\Tests\ScrollBench.cpp', 'Test\EcoSbor\Tests\ScrollSettleProbe.cpp', 'Test\EcoSbor\Tests\WarmupBench.cpp')) {
    $name = [System.IO.Path]::GetFileNameWithoutExtension($source)
    $command = '"{0}" >nul && cl /nologo /std:c++20 /utf-8 /EHsc /MDd /W4 /WX /I BeastSDK\Source /I Test\EcoSbor\Source "{1}" Test\EcoSbor\Intermediate\Verification\EcoScene.obj /FoTest\EcoSbor\Intermediate\Verification\{2}.obj /FeTest\EcoSbor\Intermediate\Verification\{2}.exe Test\EcoSbor\Intermediate\Build\BeastSDK\Windows\Debug\Beast.lib user32.lib gdi32.lib' -f $vcvars, $source, $name
    & cmd /c $command
    if ($LASTEXITCODE -ne 0) { throw "$name compilation failed." }
    & "./Test/EcoSbor/Intermediate/Verification/$name.exe" 2>&1 | Tee-Object -FilePath "Test/EcoSbor/Intermediate/Verification/$name.log"
    if ($LASTEXITCODE -ne 0) { throw "$name failed." }
}
