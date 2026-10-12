using System;
using System.Collections.Generic;
using System.ComponentModel.Composition;
using System.IO;
using System.Threading.Tasks;
using Microsoft.VisualStudio.ProjectSystem;
using Microsoft.VisualStudio.ProjectSystem.Debug;
using Microsoft.VisualStudio.ProjectSystem.VS.Debug;

namespace Beast.AndroidLauncher;

[ExportDebugger("BeastWindows")]
[AppliesTo("BeastBuildTool")]
public sealed class WindowsDebugLaunchProvider : DebugLaunchProviderBase
{
    private readonly ConfiguredProject project;
    [ImportingConstructor]
    public WindowsDebugLaunchProvider(ConfiguredProject project) : base(project) { this.project = project; }
    public override Task<bool> CanLaunchAsync(DebugLaunchOptions options) =>
        Task.FromResult(AndroidEmulatorDebugLaunchProvider.GetProjectFilePath(project) is string file && File.Exists(file));

    public override Task<IReadOnlyList<IDebugLaunchSettings>> QueryDebugTargetsAsync(DebugLaunchOptions options)
    {
        string file = AndroidEmulatorDebugLaunchProvider.GetProjectFilePath(project) ?? throw new InvalidOperationException("Missing Beast project path.");
        var launch = HotReloadLaunch.Read(file) ?? throw new InvalidOperationException("Missing Beast launch metadata. Regenerate the project with beast generate.");
        string configuration = AndroidEmulatorDebugLaunchProvider.GetActiveConfiguration(project);
        // Read at every launch: no solution regeneration when toggling JSON.
        bool watch = WindowsLaunchPolicy.UseWatch(Path.Combine(launch.ProjectPath, "Config", "BuildConfig.json"), configuration);
        if (watch && launch.IsWatching()) throw new InvalidOperationException("Windows hot reload is already running for this project. Use its existing window.");
        var console = watch ? launch.StartInfo() : null;
        var settings = new DebugLaunchSettings(watch ? options | DebugLaunchOptions.NoDebug : options)
        {
            LaunchOperation = DebugLaunchOperation.CreateProcess,
            Executable = console?.FileName ?? Path.Combine(launch.ProjectPath, "Binaries", "Windows", configuration, Path.GetFileNameWithoutExtension(file) + ".exe"),
            Arguments = console?.Arguments ?? "",
            CurrentDirectory = launch.ProjectPath,
            LaunchDebugEngineGuid = DebuggerEngines.NativeOnlyEngine
        };
        return Task.FromResult<IReadOnlyList<IDebugLaunchSettings>>(new[] { settings });
    }
}
