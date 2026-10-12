param([string]$Project = 'Test/ResourcesDemo')
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$projectRoot = [IO.Path]::GetFullPath((Join-Path $repo $Project))
$verification = Join-Path $projectRoot 'Intermediate/Verification'
New-Item -ItemType Directory -Force -Path $verification | Out-Null
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'MSVC installation not found.' }
$vcvars = Join-Path $vs 'VC/Auxiliary/Build/vcvars64.bat'
$sdk = Join-Path $repo 'BeastSDK'
$library = Join-Path $projectRoot 'Intermediate/Build/BeastSDK/Windows/Debug/Beast.lib'
$source = Join-Path $projectRoot 'Source'
$shaders = Join-Path $projectRoot 'Intermediate/Build/Windows/Debug/Shaders'
if (-not (Test-Path $library)) { throw 'Build ResourcesDemo Windows Debug first.' }
foreach ($name in @('OpenGLLoaderTests', 'OpenGLFrameTests', 'OpenGLFrameGpuTests', 'RHITests', 'RHIResourceTests')) {
    $cpp = Join-Path $PSScriptRoot ($name + '.cpp')
    $object = Join-Path $verification ($name + '.obj')
    $exe = Join-Path $verification ($name + '.exe')
    $command = '"{0}" >nul && cl /nologo /std:c++20 /EHsc /MDd /W4 /WX /I "{1}/Source" /I "{1}/ThirdParty/OpenGL" /I "{2}" /I "{3}" "{4}" /Fo"{5}" /Fe"{6}" "{7}" user32.lib gdi32.lib' -f $vcvars, $sdk, $source, $shaders, $cpp, $object, $exe, $library
    & cmd /c $command
    if ($LASTEXITCODE -ne 0) { throw "$name compilation failed." }
    & $exe 2>&1 | Tee-Object -FilePath (Join-Path $verification ($name + '.log'))
    if ($LASTEXITCODE -ne 0) { throw "$name failed." }
}
