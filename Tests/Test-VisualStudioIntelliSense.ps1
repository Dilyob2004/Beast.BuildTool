param([string]$ProjectFile = 'Test/EcoSbor/EcoSbor.vcxproj')
$ErrorActionPreference = 'Stop'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'Visual Studio C++ installation not found.' }
$msbuild = Join-Path $vs 'MSBuild/Current/Bin/MSBuild.exe'
$projectPath = (Resolve-Path -LiteralPath $ProjectFile).Path
$projects = @($projectPath)
[xml]$xml = Get-Content -LiteralPath $projectPath -Raw
foreach ($reference in $xml.Project.ItemGroup.ProjectReference) {
    if ($reference -and $reference.Include -like '*BeastSDK.vcxproj') {
        $projects += [IO.Path]::GetFullPath([IO.Path]::Combine([IO.Path]::GetDirectoryName($projectPath), $reference.Include))
    }
}
foreach ($project in $projects) {
    foreach ($configuration in @('Debug', 'Release')) {
        $raw = & $msbuild $project /nologo "/p:Configuration=$configuration" /p:Platform=x64 /p:DesignTimeBuild=true /t:GetClCommandLines,GetProjectDirectories /getProperty:BeastSdkRoot /getItem:ClCommandLines,_ProjectDirectories
        if ($LASTEXITCODE -ne 0) { throw "Design-time build failed: $project ($configuration)" }
        $data = ($raw -join "`n") | ConvertFrom-Json
        if (-not $data.Items.ClCommandLines) { throw 'No IntelliSense compiler commands were produced.' }
        foreach ($command in $data.Items.ClCommandLines) {
            if ($command.Identity -notmatch '/std:c\+\+20' -or $command.Identity -notmatch '/utf-8') {
                throw "Missing C++20/UTF-8: $project ($configuration)"
            }
            $expectedDefine = if ($configuration -eq 'Debug') { '_DEBUG' } else { 'NDEBUG' }
            if ($command.Identity -notmatch "/D $expectedDefine( |$)") { throw "Missing $expectedDefine" }
        }
        if ($data.Properties.BeastSdkRoot) {
            $sdkSource = Join-Path $data.Properties.BeastSdkRoot 'Source'
            $includes = $data.Items._ProjectDirectories[0].IncludePath -split ';'
            if ($sdkSource -notin $includes -or -not (Test-Path -LiteralPath (Join-Path $sdkSource 'Beast.h'))) {
                throw "Beast SDK include is missing or invalid: $sdkSource"
            }
        }
        Write-Output "$([IO.Path]::GetFileName($project)) $configuration : design-time C++20, UTF-8, defines and SDK paths PASS"
    }
}
