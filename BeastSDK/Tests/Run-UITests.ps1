$ErrorActionPreference = 'Stop'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'MSVC not found.' }
$vcvars = Join-Path $vs 'VC/Auxiliary/Build/vcvars64.bat'
New-Item -ItemType Directory -Force Test/ButtonDemo/Intermediate/Verification | Out-Null
foreach ($name in @('UILayoutTests', 'UITests', 'UIRasterTests')) {
    $command = '"{0}" >nul && cl /nologo /std:c++20 /utf-8 /EHsc /MDd /W4 /WX /I BeastSDK\Source /I BeastSDK\ThirdParty\OpenGL /I Test\ButtonDemo\Source BeastSDK\Tests\{1}.cpp /FoTest\ButtonDemo\Intermediate\Verification\{1}.obj /FeTest\ButtonDemo\Intermediate\Verification\{1}.exe Test\ButtonDemo\Intermediate\Build\BeastSDK\Windows\Debug\Beast.lib user32.lib gdi32.lib' -f $vcvars, $name
    & cmd /c $command
    if ($LASTEXITCODE -ne 0) { throw "$name compilation failed." }
    & "./Test/ButtonDemo/Intermediate/Verification/$name.exe" 2>&1 | Tee-Object -FilePath "Test/ButtonDemo/Intermediate/Verification/$name.log"
    if ($LASTEXITCODE -ne 0) { throw "$name failed." }
}
