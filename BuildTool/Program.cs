using Beast.BuildTool;

internal static class Program
{
    private static async Task<int> Main(string[] args)
    {
        Log.Init();
        try
        {
            var commandLine = new CommandLine(args);
            string command = commandLine.Positionals.FirstOrDefault()?.ToLowerInvariant() ?? "";
            return command switch
            {
                "new" => CreateProject(commandLine),
                "module" => CreateModule(commandLine),
                "generate" => Generate(commandLine),
                "build" => await Build(commandLine),
                "sdk" => await BuildSdk(commandLine),
                "run" => await Run(commandLine),
                "watch" => await Watch(commandLine),
                "clean" => Clean(commandLine),
                "help" or "--help" or "-h" or "" => Help(),
                _ => UnknownCommand(command)
            };
        }
        catch (Exception ex)
        {
            Log.TraceError(ex.Message);
            return 1;
        }
        finally
        {
            Log.Cleanup();
        }
    }

    private static int CreateProject(CommandLine args)
    {
        string? name = args.Positionals.Skip(1).FirstOrDefault();
        if (string.IsNullOrWhiteSpace(name)) return Usage("Project name is required.");
        string location = args.HasArgument("path") ? args.GetArgument("path") : Environment.CurrentDirectory;
        return ProjectGenerator.CreateProject(name, location) ? 0 : 1;
    }

    private static int CreateModule(CommandLine args)
    {
        string? name = args.Positionals.Skip(1).FirstOrDefault();
        if (string.IsNullOrWhiteSpace(name)) return Usage("Module name is required: beast module <Name> [project-path].");
        bool explicitProject = args.HasArgument("project") || args.Positionals.Count > 2;
        string? project = explicitProject ? ResolveProject(args, 2) : Environment.CurrentDirectory;
        if (project == null || !Directory.Exists(Path.Combine(project, "Source", "Launch")) ||
            !File.Exists(Path.Combine(project, "Config", "BuildConfig.json")))
            return Usage("Beast project not found. Run this command in the project root (beside its .sln and Source), or supply a project path.");
        if (!ProjectGenerator.CreateModule(project, name)) return 1;
        SolutionGenerator.Generate(project, new DirectoryInfo(project).Name);
        Log.TraceInfo("Visual Studio project and file filters updated.");
        return 0;
    }

    private static int Generate(CommandLine args)
    {
        string? project = ResolveProject(args, 1);
        if (project == null) return Usage("Project path is required.");
        string name = new DirectoryInfo(project).Name;
        SolutionGenerator.Generate(project, name);
        Log.TraceInfo($"Generated {Path.Combine(project, name + ".sln")}");
        return 0;
    }

    private static async Task<int> Build(CommandLine args)
    {
        string? project = ResolveProject(args, 1);
        if (project == null) return Usage("Project path is required.");
        SetProject(project);
        BuildConfigManager.LoadConfig(project);
        Platform platform = CreatePlatform(args.HasArgument("platform") ? args.GetArgument("platform") : BuildConfigManager.Config.DefaultPlatform);
        TargetConfiguration configuration = ParseConfiguration(args.HasArgument("config") ? args.GetArgument("config") : BuildConfigManager.Config.DefaultConfiguration);
        string projectName = new DirectoryInfo(project).Name;
        await new GenericProjectBuilder().BuildAsync(project, projectName, platform, configuration);
        if (args.HasFlag("run"))
        {
            if (platform.Type != PlatformType.Android)
                throw new ArgumentException("--run is currently supported only with --platform Android.");
            await AndroidRunner.RunAsync(project, projectName, configuration, GetOptionalArgument(args, "avd"), GetOptionalArgument(args, "device"), args.HasFlag("wait"));
        }
        return 0;
    }

    private static async Task<int> BuildSdk(CommandLine args)
    {
        string? project = ResolveProject(args, 1);
        if (project == null) return Usage("Project path is required for SDK build.");
        SetProject(project);
        BuildConfigManager.LoadConfig(project);
        Platform platform = CreatePlatform(GetOptionalArgument(args, "platform") ?? BuildConfigManager.Config.DefaultPlatform);
        TargetConfiguration configuration = ParseConfiguration(GetOptionalArgument(args, "config") ?? BuildConfigManager.Config.DefaultConfiguration);
        await EngineSdkBuilder.BuildAsync(project, platform, configuration);
        return 0;
    }

    private static async Task<int> Watch(CommandLine args)
    {
        string? project = ResolveProject(args, 1);
        if (project == null && File.Exists(Path.Combine(Environment.CurrentDirectory, "Config", "BuildConfig.json"))) project = Environment.CurrentDirectory;
        if (project == null) return Usage("Run beast watch in a project root or provide its path.");
        if (args.HasArgument("config") && !args.GetArgument("config").Equals("Debug", StringComparison.OrdinalIgnoreCase))
            throw new ArgumentException("Hot reload is a separate Debug/dev mode, not a Release updater.");
        SetProject(project); BuildConfigManager.LoadConfig(project);
        await HotReloadWatcher.Run(project, GetOptionalArgument(args, "platform") ?? "Windows", GetOptionalArgument(args, "avd"), GetOptionalArgument(args, "device"), args.HasFlag("prepare-only"));
        return 0;
    }

    private static async Task<int> Run(CommandLine args)
    {
        string? project = ResolveProject(args, 1);
        if (project == null) return Usage("Project path is required.");
        string platformName = args.HasArgument("platform") ? args.GetArgument("platform") : "Android";
        if (!platformName.Equals("Android", StringComparison.OrdinalIgnoreCase))
            throw new ArgumentException("The run command currently supports only --platform Android.");

        SetProject(project);
        BuildConfigManager.LoadConfig(project);
        TargetConfiguration configuration = ParseConfiguration(args.HasArgument("config") ? args.GetArgument("config") : BuildConfigManager.Config.DefaultConfiguration);
        string projectName = new DirectoryInfo(project).Name;
        if (!args.HasFlag("no-build"))
            await new GenericProjectBuilder().BuildAsync(project, projectName, new AndroidPlatform(), configuration);
        await AndroidRunner.RunAsync(project, projectName, configuration, GetOptionalArgument(args, "avd"), GetOptionalArgument(args, "device"), args.HasFlag("wait"));
        return 0;
    }

    private static int Clean(CommandLine args)
    {
        string? project = ResolveProject(args, 1);
        if (project == null) return Usage("Project path is required.");
        string intermediate = Path.Combine(project, "Intermediate", "Build");
        if (Directory.Exists(intermediate)) Directory.Delete(intermediate, true);
        Log.TraceInfo($"Cleaned {intermediate}");
        return 0;
    }

    private static Platform CreatePlatform(string value) => value.ToLowerInvariant() switch
    {
        "windows" or "win64" => new WindowsPlatform(),
        "android" => new AndroidPlatform(),
        _ => throw new ArgumentException($"Unsupported platform '{value}'. Use Windows or Android.")
    };

    private static TargetConfiguration ParseConfiguration(string value) => value.ToLowerInvariant() switch
    {
        "debug" => TargetConfiguration.Debug,
        "development" => TargetConfiguration.Development,
        "release" => TargetConfiguration.Release,
        _ => throw new ArgumentException($"Unsupported configuration '{value}'. Use Debug, Development, or Release.")
    };

    private static string? ResolveProject(CommandLine args, int positionalIndex)
    {
        string? raw = args.HasArgument("project") ? args.GetArgument("project") : args.Positionals.Skip(positionalIndex).FirstOrDefault();
        if (string.IsNullOrWhiteSpace(raw)) return null;
        string path = Path.GetFullPath(raw);
        return Directory.Exists(path) ? path : null;
    }

    private static void SetProject(string path)
    {
        GlobalEnvironment.ProjectPath = path;
        GlobalEnvironment.ProjectName = new DirectoryInfo(path).Name;
    }

    private static string? GetOptionalArgument(CommandLine args, string name) => args.HasArgument(name) ? args.GetArgument(name) : null;

    private static int UnknownCommand(string command) => Usage($"Unknown command '{command}'.");
    private static int Usage(string error)
    {
        Log.TraceError(error);
        Help();
        return 1;
    }

    private static int Help()
    {
        Console.WriteLine("""
Beast BuildTool
  beast new <Name> [--path <directory>]
  beast module <Name> [project-path]   (defaults to the current project root; also accepts --project <path>)
  beast generate <project-path>
  beast sdk <project-path> [--platform Windows|Android] [--config Debug|Release]
  beast build <project-path> [--platform Windows|Android] [--config Debug|Development|Release] [--run --avd <name> --wait]
  beast run <project-path> [--platform Android] [--config Debug|Development|Release] [--avd <name>] [--device <serial>] [--no-build] [--wait]
  beast clean <project-path>
  beast watch [project-path] [--platform Windows|Android] [--avd <name>] [--device <emulator-serial>] [--prepare-only]
""");
        return 0;
    }
}
