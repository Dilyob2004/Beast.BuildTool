using System.Text;
using System.Xml.Linq;
using System.Security.Cryptography;

namespace Beast.BuildTool;

/// <summary>Creates a Visual Studio Makefile project that delegates builds to this tool.</summary>
public static class SolutionGenerator
{
    public static void Generate(string projectDir, string projectName)
    {
        projectDir = Path.GetFullPath(projectDir);
        BuildConfigManager.LoadConfig(projectDir);
        string vcxproj = Path.Combine(projectDir, projectName + ".vcxproj");
        string projectGuid = GetProjectGuid(vcxproj);
        string? sdkRoot = BuildConfigManager.Config.Engine.Enabled ? EngineSdkBuilder.ResolveRoot(projectDir) : null;
        string sdkProject = Path.Combine(projectDir, "Intermediate", "VisualStudio", "BeastSDK.vcxproj");
        string sdkGuid = GetProjectGuid(sdkProject);
        string androidDebuggerRule = Path.Combine(projectDir, "Intermediate", "VisualStudio", "BeastAndroidDebugger.xml");
        Directory.CreateDirectory(Path.GetDirectoryName(androidDebuggerRule)!);
        File.WriteAllText(androidDebuggerRule, AndroidDebuggerRule, Encoding.UTF8);
        File.WriteAllText(Path.Combine(Path.GetDirectoryName(androidDebuggerRule)!, "BeastWindowsDebugger.xml"), WindowsDebuggerRule, Encoding.UTF8);
        string exe = Environment.ProcessPath ?? "Beast.BuildTool.exe";
        string toolCommand = Path.GetFileNameWithoutExtension(exe).Equals("dotnet", StringComparison.OrdinalIgnoreCase)
            ? $"\"{exe}\" \"{typeof(SolutionGenerator).Assembly.Location}\""
            : $"\"{exe}\"";
        string escapedProject = Escape(projectDir);
        string escapedToolCommand = Escape(toolCommand);
        string includePaths = "$(ProjectDir)Source;$(ProjectDir)Intermediate\\Build\\Windows\\$(Configuration)\\Shaders;" + (sdkRoot == null ? "" : "$(BeastSdkRoot)\\Source;");
        string sdkReference = sdkRoot == null ? "" : $$"""
              <ItemGroup>
                <ProjectReference Include="Intermediate\VisualStudio\BeastSDK.vcxproj">
                  <Project>{{sdkGuid}}</Project>
                  <ReferenceOutputAssembly>false</ReferenceOutputAssembly>
                  <LinkLibraryDependencies>false</LinkLibraryDependencies>
                </ProjectReference>
              </ItemGroup>
            """;
        var items = CollectProjectItems(projectDir);
        string projectItems = CreateProjectItems(items);
        string xml = $$"""
            <?xml version="1.0" encoding="utf-8"?>
            <Project DefaultTargets="Build" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
              <ItemGroup Label="ProjectConfigurations"><ProjectConfiguration Include="Debug|x64"><Configuration>Debug</Configuration><Platform>x64</Platform></ProjectConfiguration><ProjectConfiguration Include="Release|x64"><Configuration>Release</Configuration><Platform>x64</Platform></ProjectConfiguration></ItemGroup>
              <PropertyGroup Label="Globals"><ProjectGuid>{{projectGuid}}</ProjectGuid><Keyword>MakeFileProj</Keyword></PropertyGroup>
              <Import Project="$(VCTargetsPath)\Microsoft.Cpp.Default.props" />
              <PropertyGroup Condition="'$(Configuration)'=='Debug'" Label="Configuration"><ConfigurationType>Makefile</ConfigurationType><PlatformToolset>v143</PlatformToolset></PropertyGroup>
              <PropertyGroup Condition="'$(Configuration)'=='Release'" Label="Configuration"><ConfigurationType>Makefile</ConfigurationType><PlatformToolset>v143</PlatformToolset></PropertyGroup>
            {{CreateIntelliSenseProperties(sdkRoot, isSdk: false)}}
              <Import Project="$(VCTargetsPath)\Microsoft.Cpp.props" />
              <PropertyGroup>
                <OutDir>{{escapedProject}}\Binaries\Windows\$(Configuration)\</OutDir>
                <IntDir>{{escapedProject}}\Intermediate\Build\VisualStudio\$(Platform)\$(Configuration)\</IntDir>
                <BeastBuildToolPath>{{Escape(exe)}}</BeastBuildToolPath>
                <BeastBuildToolAssemblyPath>{{Escape(typeof(SolutionGenerator).Assembly.Location)}}</BeastBuildToolAssemblyPath>
                <BeastProjectPath>{{escapedProject}}</BeastProjectPath>
                <DebuggerFlavor>BeastWindows</DebuggerFlavor>
                <NMakeIncludeSearchPath>{{includePaths}}$(NMakeIncludeSearchPath)</NMakeIncludeSearchPath>
                <NMakeBuildCommandLine>{{escapedToolCommand}} build "{{escapedProject}}" --platform Windows --config $(Configuration)</NMakeBuildCommandLine>
                <NMakeCleanCommandLine>{{escapedToolCommand}} clean "{{escapedProject}}"</NMakeCleanCommandLine>
                <NMakeOutput>{{escapedProject}}\Binaries\Windows\$(Configuration)\{{projectName}}.exe</NMakeOutput>
              </PropertyGroup>
              <ItemGroup>
                <ProjectCapability Include="BeastBuildTool" />
                <PropertyPageSchema Include="$(ProjectDir)Intermediate\VisualStudio\BeastAndroidDebugger.xml">
                  <Context>Project</Context>
                </PropertyPageSchema>
                <PropertyPageSchema Include="$(ProjectDir)Intermediate\VisualStudio\BeastWindowsDebugger.xml">
                  <Context>Project</Context>
                </PropertyPageSchema>
                <None Include="Config\BuildConfig.json" />
              </ItemGroup>
            {{projectItems}}
            {{sdkReference}}
              <Import Project="$(VCTargetsPath)\Microsoft.Cpp.targets" />
            </Project>
            """;
        File.WriteAllText(vcxproj, xml, Encoding.UTF8);
        WriteProjectFilters(vcxproj + ".filters", items);
        string sdkDependency = "";
        string sdkSolutionEntry = "";
        string sdkConfigurations = "";
        if (sdkRoot != null)
        {
            WriteSdkProject(sdkProject, sdkGuid, sdkRoot, projectDir, escapedToolCommand);
            sdkDependency = $"\tProjectSection(ProjectDependencies) = postProject{Environment.NewLine}\t\t{sdkGuid} = {sdkGuid}{Environment.NewLine}\tEndProjectSection{Environment.NewLine}";
            sdkSolutionEntry = $"Project(\"{{BC8A1FFA-BEE3-4634-8014-F334798102B3}}\") = \"BeastSDK\", \"Intermediate\\VisualStudio\\BeastSDK.vcxproj\", \"{sdkGuid}\"{Environment.NewLine}EndProject{Environment.NewLine}";
            sdkConfigurations = CreateSolutionConfigurations(sdkGuid);
        }
        string sln = $$"""
            Microsoft Visual Studio Solution File, Format Version 12.00
            # Visual Studio Version 17
            Project("{BC8A1FFA-BEE3-4634-8014-F334798102B3}") = "{{projectName}}", "{{projectName}}.vcxproj", "{{projectGuid}}"
            {{sdkDependency}}EndProject
            {{sdkSolutionEntry}}Global
            	GlobalSection(SolutionConfigurationPlatforms) = preSolution
            		Debug|x64 = Debug|x64
            		Release|x64 = Release|x64
            	EndGlobalSection
            	GlobalSection(ProjectConfigurationPlatforms) = postSolution
            {{sdkConfigurations}}{{CreateSolutionConfigurations(projectGuid)}}	EndGlobalSection
            EndGlobal
            """;
        File.WriteAllText(Path.Combine(projectDir, projectName + ".sln"), sln, Encoding.UTF8);
    }

    private static void WriteSdkProject(string projectFile, string projectGuid, string sdkRoot, string projectDir, string escapedToolCommand)
    {
        var items = CollectProjectItems(sdkRoot, useAbsolutePaths: true);
        string escapedProject = Escape(projectDir);
        string xml = $$"""
            <?xml version="1.0" encoding="utf-8"?>
            <Project DefaultTargets="Build" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
              <ItemGroup Label="ProjectConfigurations"><ProjectConfiguration Include="Debug|x64"><Configuration>Debug</Configuration><Platform>x64</Platform></ProjectConfiguration><ProjectConfiguration Include="Release|x64"><Configuration>Release</Configuration><Platform>x64</Platform></ProjectConfiguration></ItemGroup>
              <PropertyGroup Label="Globals"><ProjectGuid>{{projectGuid}}</ProjectGuid><ProjectName>BeastSDK</ProjectName><Keyword>MakeFileProj</Keyword></PropertyGroup>
              <Import Project="$(VCTargetsPath)\Microsoft.Cpp.Default.props" />
              <PropertyGroup Label="Configuration"><ConfigurationType>Makefile</ConfigurationType><PlatformToolset>v143</PlatformToolset></PropertyGroup>
            {{CreateIntelliSenseProperties(sdkRoot, isSdk: true)}}
              <Import Project="$(VCTargetsPath)\Microsoft.Cpp.props" />
              <PropertyGroup>
                <OutDir>{{escapedProject}}\Intermediate\Build\BeastSDK\Windows\$(Configuration)\</OutDir>
                <IntDir>{{escapedProject}}\Intermediate\Build\VisualStudio\BeastSDK\$(Platform)\$(Configuration)\</IntDir>
                <NMakeIncludeSearchPath>$(BeastSdkRoot)\Source;$(BeastSdkRoot)\ThirdParty\OpenGL;$(BeastSdkRoot)\ThirdParty\Vulkan-Headers\include;$(NMakeIncludeSearchPath)</NMakeIncludeSearchPath>
                <NMakeBuildCommandLine>{{escapedToolCommand}} sdk "{{escapedProject}}" --platform Windows --config $(Configuration)</NMakeBuildCommandLine>
                <NMakeCleanCommandLine>echo BeastSDK cache is cleaned by the application clean command.</NMakeCleanCommandLine>
              </PropertyGroup>
              <PropertyGroup Condition="'$(Configuration)'=='Debug'"><NMakeOutput>{{Escape(EngineSdkBuilder.GetLibraryPath(projectDir, PlatformType.Windows, TargetConfiguration.Debug))}}</NMakeOutput></PropertyGroup>
              <PropertyGroup Condition="'$(Configuration)'=='Release'"><NMakeOutput>{{Escape(EngineSdkBuilder.GetLibraryPath(projectDir, PlatformType.Windows, TargetConfiguration.Release))}}</NMakeOutput></PropertyGroup>
            {{CreateProjectItems(items)}}
              <Import Project="$(VCTargetsPath)\Microsoft.Cpp.targets" />
            </Project>
            """;
        File.WriteAllText(projectFile, xml, Encoding.UTF8);
        WriteProjectFilters(projectFile + ".filters", items);
    }

    private static string CreateIntelliSenseProperties(string? sdkRoot, bool isSdk)
    {
        // Microsoft.Cpp.props imports Microsoft.Makefile.props, which copies
        // these properties into ClCompile metadata. Define them BEFORE that
        // import; otherwise the IDE's design-time compiler sees empty options.
        // Actual compilation is still performed by our C++20/UTF-8 Builder.
        string sdkProperty = sdkRoot == null ? "" : $"<BeastSdkRoot>{Escape(sdkRoot)}</BeastSdkRoot>";
        string definitions = "PLATFORM_WINDOWS;PLATFORM_WIN32;WIN32;WIN64;_WINDOWS;_CRT_SECURE_NO_WARNINGS";
        if (isSdk) definitions += ";BEAST_STATIC=1";
        string debugDefinitions = isSdk ? "_DEBUG" : "DEBUG;_DEBUG";
        return $$"""
              <PropertyGroup Label="BeastIntelliSense">
                {{sdkProperty}}
                <AdditionalOptions>$(AdditionalOptions) /std:c++20 /utf-8</AdditionalOptions>
                <NMakePreprocessorDefinitions>$(NMakePreprocessorDefinitions);{{definitions}}</NMakePreprocessorDefinitions>
              </PropertyGroup>
              <PropertyGroup Condition="'$(Configuration)'=='Debug'">
                <NMakePreprocessorDefinitions>$(NMakePreprocessorDefinitions);{{debugDefinitions}}</NMakePreprocessorDefinitions>
              </PropertyGroup>
              <PropertyGroup Condition="'$(Configuration)'=='Release'">
                <NMakePreprocessorDefinitions>$(NMakePreprocessorDefinitions);NDEBUG</NMakePreprocessorDefinitions>
              </PropertyGroup>
            """;
    }

    private static string CreateSolutionConfigurations(string projectGuid) =>
        string.Join(Environment.NewLine, new[] { "Debug", "Release" }.SelectMany(configuration => new[]
        {
            $"\t\t{projectGuid}.{configuration}|x64.ActiveCfg = {configuration}|x64",
            $"\t\t{projectGuid}.{configuration}|x64.Build.0 = {configuration}|x64"
        })) + Environment.NewLine;

    private static string GetProjectGuid(string projectFile)
    {
        // Preserve VS startup-project and debugger selections when regenerating an existing project.
        if (File.Exists(projectFile))
        {
            try
            {
                XNamespace ns = "http://schemas.microsoft.com/developer/msbuild/2003";
                string? existing = XDocument.Load(projectFile).Descendants(ns + "ProjectGuid").FirstOrDefault()?.Value;
                if (Guid.TryParse(existing, out Guid guid))
                    return guid.ToString("B").ToUpperInvariant();
            }
            catch (System.Xml.XmlException)
            {
                // A malformed generated project can still be regenerated with a stable identity.
            }
        }

        byte[] hash = SHA256.HashData(Encoding.UTF8.GetBytes(Path.GetFullPath(projectFile).ToUpperInvariant()));
        return new Guid(hash.AsSpan(0, 16)).ToString("B").ToUpperInvariant();
    }

    private const string WindowsDebuggerRule = """
        <?xml version="1.0" encoding="utf-8"?>
        <Rule Name="BeastWindows" DisplayName="Windows (Beast)" PageTemplate="debugger"
              Description="Reads hotReload from Config/BuildConfig.json at launch: Debug watch or normal native executable."
              xmlns="http://schemas.microsoft.com/build/2009/properties">
          <Rule.DataSource><DataSource Persistence="UserFile" /></Rule.DataSource>
        </Rule>
        """;

    private const string AndroidDebuggerRule = """
        <?xml version="1.0" encoding="utf-8"?>
        <Rule Name="BeastAndroidEmulator"
              DisplayName="Android Emulator"
              PageTemplate="debugger"
              Description="Builds, installs, and launches the Android NativeActivity through Beast BuildTool."
              xmlns="http://schemas.microsoft.com/build/2009/properties">
          <Rule.DataSource>
            <DataSource Persistence="UserFile" />
          </Rule.DataSource>
          <StringProperty Name="BeastAndroidAvd"
                          DisplayName="Android virtual device (AVD)"
                          Description="Optional AVD name. If empty, Beast BuildTool uses android.defaultAvd from Config/BuildConfig.json." />
          <StringProperty Name="BeastAndroidDevice"
                          DisplayName="Android device serial"
                          Description="Optional adb device serial. Use this when more than one emulator or device is connected." />
        </Rule>
        """;

    private sealed record ProjectItem(string ItemType, string IncludePath, string LogicalPath);

    private static List<ProjectItem> CollectProjectItems(string projectDir, bool useAbsolutePaths = false)
    {
        string sourceDirectory = Path.Combine(projectDir, "Source");
        if (!Directory.Exists(sourceDirectory))
            return [];

        return Directory.EnumerateFiles(sourceDirectory, "*", SearchOption.AllDirectories)
            .Select(path => new { Path = path, Extension = Path.GetExtension(path).ToLowerInvariant() })
            .Where(file => file.Extension is ".c" or ".cc" or ".cpp" or ".cxx" or ".h" or ".hh" or ".hpp" or ".hxx" or ".cs" or ".hlsl" or ".hlsli")
            .OrderBy(file => file.Path, StringComparer.OrdinalIgnoreCase)
            .Select(file =>
            {
                string itemType = file.Extension is ".c" or ".cc" or ".cpp" or ".cxx"
                    ? "ClCompile"
                    : file.Extension is ".cs" or ".hlsl" or ".hlsli" ? "None" : "ClInclude";
                string relativePath = Path.GetRelativePath(projectDir, file.Path).Replace('/', '\\');
                return new ProjectItem(itemType, useAbsolutePaths ? Path.GetFullPath(file.Path) : relativePath, relativePath);
            })
            .ToList();
    }

    private static string CreateProjectItems(List<ProjectItem> items)
    {
        return items.Count == 0
            ? string.Empty
            : "  <ItemGroup>" + Environment.NewLine + string.Join(Environment.NewLine,
                items.Select(item => $"    <{item.ItemType} Include=\"{Escape(item.IncludePath)}\" />"))
                + Environment.NewLine + "  </ItemGroup>";
    }

    private static void WriteProjectFilters(string path, List<ProjectItem> items)
    {
        XNamespace ns = "http://schemas.microsoft.com/developer/msbuild/2003";
        var folders = new SortedSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (var item in items)
        {
            string folder = item.LogicalPath;
            while (folder.LastIndexOf('\\') is int separator && separator >= 0)
            {
                folder = folder[..separator];
                folders.Add(folder);
            }
        }

        var root = new XElement(ns + "Project", new XAttribute("ToolsVersion", "4.0"));
        if (folders.Count > 0)
            root.Add(new XElement(ns + "ItemGroup", folders.Select(folder =>
                new XElement(ns + "Filter", new XAttribute("Include", folder)))));

        if (items.Count > 0)
            root.Add(new XElement(ns + "ItemGroup", items.Select(item =>
            {
                var element = new XElement(ns + item.ItemType, new XAttribute("Include", item.IncludePath));
                int separator = item.LogicalPath.LastIndexOf('\\');
                if (separator >= 0)
                    element.Add(new XElement(ns + "Filter", item.LogicalPath[..separator]));
                return element;
            })));

        new XDocument(new XDeclaration("1.0", "utf-8", null), root).Save(path);
    }

    private static string Escape(string value) => value
        .Replace("&", "&amp;")
        .Replace("\"", "&quot;")
        .Replace("<", "&lt;")
        .Replace(">", "&gt;");
}
