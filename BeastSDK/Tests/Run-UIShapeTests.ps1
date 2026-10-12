$ErrorActionPreference = 'Stop'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'MSVC not found.' }
$vcvars = Join-Path $vs 'VC/Auxiliary/Build/vcvars64.bat'
New-Item -ItemType Directory -Force Test/EcoSbor/Intermediate/Verification | Out-Null
foreach ($name in @('UIShapeTests', 'ClipSeamProbe', 'LayerTargetPoolTests', 'OpenGLTimerTests', 'UILayerTests', 'UIRepaintTests', 'UIGlyphAtlasTests')) {
    $command = '"{0}" >nul && cl /nologo /std:c++20 /utf-8 /EHsc /MDd /W4 /WX /I BeastSDK\Source /I BeastSDK\ThirdParty\OpenGL BeastSDK\Tests\{1}.cpp /FoTest\EcoSbor\Intermediate\Verification\{1}.obj /FeTest\EcoSbor\Intermediate\Verification\{1}.exe Test\EcoSbor\Intermediate\Build\BeastSDK\Windows\Debug\Beast.lib user32.lib gdi32.lib' -f $vcvars, $name
    & cmd /c $command
    if ($LASTEXITCODE -ne 0) { throw "$name compilation failed." }
    & "./Test/EcoSbor/Intermediate/Verification/$name.exe" 2>&1 | Tee-Object -FilePath "Test/EcoSbor/Intermediate/Verification/$name.log"
    if ($LASTEXITCODE -ne 0) { throw "$name failed." }
}
