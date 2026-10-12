namespace Beast.BuildTool;

/// <summary>Compiles a project's Windows icon, version data, and application manifest into a .res file.</summary>
internal static class WindowsResourceCompiler
{
    public static void AddCompileCommand(
        CommandExecutor executor,
        WindowsPlatform platform,
        string projectPath,
        string projectName,
        TargetConfiguration configuration,
        LinkEnvironment linkEnvironment,
        string? outputDirectory = null)
    {
        WindowsTemplateGenerator.Ensure(projectPath, projectName);

        string resourceDirectory = Path.Combine(projectPath, "Platforms", "Windows", "Resources");
        string resourceScript = Path.Combine(resourceDirectory, projectName + ".rc");
        outputDirectory ??= Path.Combine(projectPath, "Intermediate", "Build", "Windows", configuration.ToString(), "Resources");
        string resourceFile = Path.Combine(outputDirectory, projectName + ".res");
        Directory.CreateDirectory(outputDirectory);

        if (string.IsNullOrWhiteSpace(platform.ResourceCompilerPath) || !File.Exists(platform.ResourceCompilerPath))
            throw new FileNotFoundException("Windows SDK resource compiler rc.exe was not found.", platform.ResourceCompilerPath);

        List<string> prerequisites =
        [
            resourceScript,
            Path.Combine(projectPath, "Config", "BuildConfig.json"),
            Path.Combine(resourceDirectory, "Default.ico"),
            Path.Combine(resourceDirectory, "Default-Win64.manifest"),
            Path.Combine(resourceDirectory, "Default-Win64-dpiaware.manifest")
        ];
        string dpiAware = BuildConfigManager.Config.Windows.DpiAware ? "1" : "0";
        string debug = configuration == TargetConfiguration.Debug ? " /d_DEBUG=1" : string.Empty;
        string arguments = $"/nologo /dBEAST_DPI_AWARE={dpiAware}{debug} /i\"{resourceDirectory}\" /fo\"{resourceFile}\" \"{resourceScript}\"";
        executor.AddCommand(new CommandContext(
            platform.ResourceCompilerPath,
            arguments,
            CommandType.Compile,
            [resourceFile],
            prerequisites,
            true,
            $"Compiling Windows resources for {projectName}..."));

        linkEnvironment.InputFiles.Add(resourceFile);
    }
}
