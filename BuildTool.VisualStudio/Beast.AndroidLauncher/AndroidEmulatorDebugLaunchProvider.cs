using System;
using System.Collections;
using System.Collections.Generic;
using System.ComponentModel.Composition;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Threading.Tasks;
using System.Xml.Linq;
using Microsoft.VisualStudio.ProjectSystem;
using Microsoft.VisualStudio.ProjectSystem.Debug;
using Microsoft.VisualStudio.ProjectSystem.VS.Debug;

namespace Beast.AndroidLauncher;

/// <summary>
/// Adds an Android launch target to Beast Makefile C++ projects without adding an
/// auxiliary project to the solution. The process being launched is Beast BuildTool,
/// which builds, installs, and opens the Android NativeActivity application.
/// </summary>
[ExportDebugger("BeastAndroidEmulator")]
[AppliesTo("BeastBuildTool")]
public sealed class AndroidEmulatorDebugLaunchProvider : DebugLaunchProviderBase
{
    private readonly ConfiguredProject configuredProject;

    [ImportingConstructor]
    public AndroidEmulatorDebugLaunchProvider(ConfiguredProject configuredProject)
        : base(configuredProject)
    {
        this.configuredProject = configuredProject;
    }

    public override Task<bool> CanLaunchAsync(DebugLaunchOptions launchOptions)
    {
        return Task.FromResult(TryGetLaunchInfo(out _));
    }

    public override Task<IReadOnlyList<IDebugLaunchSettings>> QueryDebugTargetsAsync(DebugLaunchOptions launchOptions)
    {
        if (!TryGetLaunchInfo(out LaunchInfo launchInfo))
            return Task.FromResult<IReadOnlyList<IDebugLaunchSettings>>(Array.Empty<IDebugLaunchSettings>());

        var settings = new DebugLaunchSettings(launchOptions)
        {
            LaunchOperation = DebugLaunchOperation.CreateProcess,
            Executable = launchInfo.ToolPath,
            Arguments = launchInfo.Arguments,
            CurrentDirectory = launchInfo.ProjectPath,
            LaunchDebugEngineGuid = DebuggerEngines.NativeOnlyEngine,
        };

        return Task.FromResult<IReadOnlyList<IDebugLaunchSettings>>(new[] { settings });
    }

    private bool TryGetLaunchInfo(out LaunchInfo launchInfo)
    {
        launchInfo = default;
        string? projectFile = GetProjectFilePath(configuredProject);
        if (string.IsNullOrWhiteSpace(projectFile) || !File.Exists(projectFile))
            return false;
        string projectFilePath = projectFile ?? string.Empty;

        try
        {
            XDocument project = XDocument.Load(projectFilePath);
            XNamespace msbuild = "http://schemas.microsoft.com/developer/msbuild/2003";
            string? toolPath = project.Descendants(msbuild + "BeastBuildToolPath").FirstOrDefault()?.Value.Trim();
            string? projectPath = project.Descendants(msbuild + "BeastProjectPath").FirstOrDefault()?.Value.Trim();

            if (string.IsNullOrWhiteSpace(toolPath) || string.IsNullOrWhiteSpace(projectPath))
                return false;

            if (!Path.IsPathRooted(toolPath))
                toolPath = Path.GetFullPath(Path.Combine(Path.GetDirectoryName(projectFilePath)!, toolPath));
            if (!Path.IsPathRooted(projectPath))
                projectPath = Path.GetFullPath(Path.Combine(Path.GetDirectoryName(projectFilePath)!, projectPath));

            if (!File.Exists(toolPath) || !Directory.Exists(projectPath))
                return false;

            string configuration = GetActiveConfiguration(configuredProject);
            launchInfo = new LaunchInfo(
                toolPath!,
                projectPath!,
                configuration,
                ReadUserProperty(projectFilePath, "BeastAndroidAvd", configuration),
                ReadUserProperty(projectFilePath, "BeastAndroidDevice", configuration));
            return true;
        }
        catch (IOException)
        {
            return false;
        }
        catch (System.Xml.XmlException)
        {
            return false;
        }
    }

    internal static string? GetProjectFilePath(object configuredProject)
    {
        object? unconfiguredProject = GetPropertyValue(configuredProject, "UnconfiguredProject");
        return GetPropertyValue(unconfiguredProject, "FullPath") as string;
    }

    internal static string GetActiveConfiguration(object configuredProject)
    {
        object? projectConfiguration = GetPropertyValue(configuredProject, "ProjectConfiguration");
        object? dimensions = GetPropertyValue(projectConfiguration, "Dimensions");

        if (dimensions is IDictionary dictionary && dictionary.Contains("Configuration"))
        {
            string? value = dictionary["Configuration"] as string;
            if (!string.IsNullOrWhiteSpace(value))
                return value!;
        }

        string? name = GetPropertyValue(projectConfiguration, "Name") as string;
        if (!string.IsNullOrWhiteSpace(name))
        {
            string configuration = name!.Split('|')[0];
            if (!string.IsNullOrWhiteSpace(configuration))
                return configuration;
        }

        return "Debug";
    }

    private static string? ReadUserProperty(string projectFile, string propertyName, string configuration)
    {
        string userFile = projectFile + ".user";
        if (!File.Exists(userFile)) return null;

        try
        {
            XDocument userProject = XDocument.Load(userFile);
            XNamespace msbuild = "http://schemas.microsoft.com/developer/msbuild/2003";
            string configurationMarker = configuration + "|";

            IEnumerable<XElement> groups = userProject
                .Descendants(msbuild + "PropertyGroup")
                .Where(group =>
                {
                    string condition = (string?)group.Attribute("Condition") ?? string.Empty;
                    return string.IsNullOrWhiteSpace(condition)
                        || condition.IndexOf(configurationMarker, StringComparison.OrdinalIgnoreCase) >= 0;
                });

            return groups
                .Elements(msbuild + propertyName)
                .Select(element => element.Value.Trim())
                .FirstOrDefault(value => !string.IsNullOrWhiteSpace(value));
        }
        catch (IOException)
        {
            return null;
        }
        catch (System.Xml.XmlException)
        {
            return null;
        }
    }

    private static object? GetPropertyValue(object? instance, string name)
    {
        return instance?.GetType().GetProperty(name, BindingFlags.Instance | BindingFlags.Public)?.GetValue(instance);
    }

    private static string Quote(string value) => $"\"{value.Replace("\"", "\\\"")}\"";

    private readonly struct LaunchInfo
    {
        public LaunchInfo(string toolPath, string projectPath, string configuration, string? avd, string? device)
        {
            ToolPath = toolPath;
            ProjectPath = projectPath;
            Configuration = configuration;
            Arguments = CreateArguments(projectPath, configuration, avd, device);
        }

        public string ToolPath { get; }
        public string ProjectPath { get; }
        public string Configuration { get; }
        public string Arguments { get; }

        private static string CreateArguments(string projectPath, string configuration, string? avd, string? device)
        {
            string arguments = $"build {Quote(projectPath)} --platform Android --config {configuration} --run --wait";
            if (!string.IsNullOrWhiteSpace(avd)) arguments += " --avd " + Quote(avd!);
            if (!string.IsNullOrWhiteSpace(device)) arguments += " --device " + Quote(device!);
            return arguments;
        }
    }
}
