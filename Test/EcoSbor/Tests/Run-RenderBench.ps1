$ErrorActionPreference = 'Stop'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'MSVC not found.' }
$vcvars = Join-Path $vs 'VC/Auxiliary/Build/vcvars64.bat'
New-Item -ItemType Directory -Force Test/EcoSbor/Intermediate/Verification | Out-Null
$command = '"{0}" >nul && cl /nologo /std:c++20 /utf-8 /EHsc /MDd /W4 /WX /I BeastSDK\Source /I Test\EcoSbor\Source /c Test\EcoSbor\Source\Launch\EcoScene.cpp /FoTest\EcoSbor\Intermediate\Verification\EcoSceneBench.obj && cl /nologo /std:c++20 /utf-8 /EHsc /MDd /W4 /WX /I BeastSDK\Source /I Test\EcoSbor\Source Test\EcoSbor\Tests\RenderBench.cpp Test\EcoSbor\Intermediate\Verification\EcoSceneBench.obj /FoTest\EcoSbor\Intermediate\Verification\RenderBench.obj /FeTest\EcoSbor\Intermediate\Verification\RenderBench.exe Test\EcoSbor\Intermediate\Build\BeastSDK\Windows\Debug\Beast.lib user32.lib gdi32.lib' -f $vcvars
& cmd /c $command
if ($LASTEXITCODE -ne 0) { throw 'RenderBench compilation failed.' }
& ./Test/EcoSbor/Intermediate/Verification/RenderBench.exe 2>&1 | Tee-Object -FilePath Test/EcoSbor/Intermediate/Verification/RenderBench.log
if ($LASTEXITCODE -ne 0) { throw 'RenderBench failed.' }
