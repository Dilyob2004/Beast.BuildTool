using System.Diagnostics;
using System.Security.Cryptography;
using System.Text;

namespace Beast.BuildTool;

/// <summary>ARM64 Android NDK/Clang backend. It builds a shared library for NativeActivity.</summary>
public sealed class AndroidPlatform : Platform
{
    public override string SharedLibraryFileExtension => ".so";
    public override string StaticLibraryFileExtension => ".a";
    public override string PlatformName => "Android";
    public override PlatformArch Architecture => _x64 ? PlatformArch.X64 : PlatformArch.ARM64;
    private readonly bool _x64;
    public string Abi => _x64 ? "x86_64" : "arm64-v8a";
    public string ToolchainRoot => _toolchainRoot;
    public override PlatformType Type => PlatformType.Android;

    private readonly string _toolchainRoot;
    private readonly string _clang;
    private readonly int _apiLevel;
    public string NdkRoot { get; }

    public AndroidPlatform(bool x64 = false)
    {
        _x64 = x64;
        var android = BuildConfigManager.Config.Android;
        _apiLevel = android.MinSdk;
        if (BuildConfigManager.Config.Engine.Enabled && _apiLevel < 29)
            throw new InvalidOperationException("Beast SDK requires Android 10+ (API 29). Set android.minSdk to 29 or higher in Config/BuildConfig.json and the project's Gradle minSdk.");
        NdkRoot = ResolveNdk(android.NdkRoot, android.NdkVersion);
        _toolchainRoot = Path.Combine(NdkRoot, "toolchains", "llvm", "prebuilt", "windows-x86_64");
        _clang = Path.Combine(_toolchainRoot, "bin", "clang++.exe");
        if (!File.Exists(_clang))
            throw new FileNotFoundException("Android NDK Clang was not found.", _clang);

        SystemIncludePaths.Add(Path.Combine(_toolchainRoot, "sysroot", "usr", "include"));
        Log.TraceInfo($"Android NDK: {NdkRoot}");
    }

    public override bool IsValid() => File.Exists(_clang);

    public override void SetupCPPEnvironment(CPPEnvironment environment)
    {
        environment.PreprocessorDefinitions.Add("PLATFORM_ANDROID");
        environment.PreprocessorDefinitions.Add("__ANDROID__");
        // Clang derives __ANDROID_API__ from the API-qualified --target.
    }

    public override void SetupLinkEnvironment(LinkEnvironment environment)
    {
        environment.InputLibraries.Add("android");
        environment.InputLibraries.Add("log");
        environment.InputLibraries.Add("c");
        environment.InputLibraries.Add("m");
        environment.InputLibraries.Add("dl");
    }

    public override CPPOutput CompileCppFiles(CommandExecutor executor, CPPEnvironment environment, List<string> sourceFiles, string outputPath)
    {
        Directory.CreateDirectory(outputPath);
        var output = new CPPOutput();
        foreach (string source in sourceFiles.Distinct(StringComparer.OrdinalIgnoreCase))
        {
            string sourceId = Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(Path.GetFullPath(source))))[..12];
            string objectFile = Path.Combine(outputPath, Path.GetFileNameWithoutExtension(source) + "_" + sourceId + ".o");
            output.ObjectFiles.Add(objectFile);
            var args = new List<string> { "-c", "-fPIC", "-ffunction-sections", "-fdata-sections", TargetArgument(), "--sysroot=\"" + Path.Combine(_toolchainRoot, "sysroot") + "\"" };
            args.Add(environment.Optimization ? "-O2" : "-O0");
            // Retain line information in optimized originals; strip deployment copies only.
            args.Add("-g");
            bool isC = Path.GetExtension(source).Equals(".c", StringComparison.OrdinalIgnoreCase);
            if (isC) args.AddRange(["-x", "c", "-std=c11"]);
            else args.Add(environment.CppVersion switch { CppVersion.Cpp14 => "-std=c++14", CppVersion.Cpp17 => "-std=c++17", _ => "-std=c++20" });
            foreach (string define in environment.PreprocessorDefinitions) args.Add("-D\"" + define + "\"");
            foreach (string include in environment.IncludePaths.Concat(SystemIncludePaths).Where(Directory.Exists)) args.Add("-I\"" + include + "\"");
            args.AddRange(environment.CustomArgs);
            args.Add("-o \"" + objectFile + "\"");
            args.Add("\"" + source + "\"");
            var prerequisites = new List<string> { source, typeof(AndroidPlatform).Assembly.Location };
            prerequisites.AddRange(environment.AdditionalPrerequisites);
            executor.AddCommand(new CommandContext(_clang, string.Join(" ", args), CommandType.Compile, new List<string> { objectFile }, prerequisites, true, $"Compiling {Path.GetFileName(source)} for Android..."));
        }
        return output;
    }

    public override void LinkFiles(CommandExecutor executor, LinkEnvironment environment, string outputFilePath)
    {
        string output = outputFilePath.EndsWith(".so", StringComparison.OrdinalIgnoreCase) ? outputFilePath : outputFilePath + ".so";
        Directory.CreateDirectory(Path.GetDirectoryName(output)!);
        var args = new List<string> { "-shared", "-fPIC", TargetArgument(), "--sysroot=\"" + Path.Combine(_toolchainRoot, "sysroot") + "\"", "-Wl,--gc-sections", "-Wl,--no-undefined", "-o \"" + output + "\"" };
        args.Add("-Wl,--build-id=sha1");
        args.AddRange(environment.CustomArgs);
        args.AddRange(environment.InputFiles.Select(x => "\"" + x + "\""));
        foreach (string path in environment.LibraryPaths) args.Add("-L\"" + path + "\"");
        foreach (string library in environment.InputLibraries) args.Add(library.Contains(Path.DirectorySeparatorChar) ? "\"" + library + "\"" : "-l" + library);
        executor.AddCommand(new CommandContext(_clang, string.Join(" ", args), CommandType.Link, new List<string> { output }, environment.InputFiles.Concat(environment.InputLibraries.Where(File.Exists)).ToList(), false, $"Linking {Path.GetFileName(output)}..."));
    }

    public override void ArchiveFiles(CommandExecutor executor, List<string> objects, string outputPath)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(outputPath)!);
        // EngineSdkBuilder invalidates archives when their member list changes.
        string args = "rcs \"" + outputPath + "\" " + string.Join(" ", objects.Select(x => "\"" + x + "\""));
        executor.AddCommand(new CommandContext(Path.Combine(_toolchainRoot, "bin", "llvm-ar.exe"), args,
            CommandType.Link, new List<string> { outputPath }, objects, false, "Archiving libBeast.a..."));
    }

    private string TargetArgument() => "--target=" + (_x64 ? "x86_64" : "aarch64") + "-linux-android" + _apiLevel;

    private static string ResolveNdk(string configuredRoot, string configuredVersion)
    {
        string root = Environment.ExpandEnvironmentVariables(configuredRoot);
        if (!Directory.Exists(root)) throw new DirectoryNotFoundException($"Android NDK path does not exist: {root}");
        if (File.Exists(Path.Combine(root, "source.properties"))) return root;
        var candidates = Directory.GetDirectories(root).OrderByDescending(Path.GetFileName).ToList();
        if (!string.IsNullOrWhiteSpace(configuredVersion)) candidates = candidates.Where(x => string.Equals(Path.GetFileName(x), configuredVersion, StringComparison.OrdinalIgnoreCase)).ToList();
        string? selected = candidates.FirstOrDefault(x => File.Exists(Path.Combine(x, "source.properties")));
        return selected ?? throw new DirectoryNotFoundException($"No Android NDK installation was found in: {root}");
    }
}
