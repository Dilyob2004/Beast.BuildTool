using System.Diagnostics;
using System.Reflection;
using System.Text.Json;

namespace Beast.BuildTool;

public sealed class GenericProjectBuilder
{
    public async Task BuildAsync(string projectPath, string projectName, Platform platform, TargetConfiguration configuration)
    {
        string sourcePath = Path.Combine(projectPath, "Source");
        var ruleFiles = Directory.GetFiles(sourcePath, "*.Build.cs", SearchOption.AllDirectories).ToList();
        if (ruleFiles.Count == 0) throw new InvalidOperationException("No *.Build.cs module definitions were found in Source.");

        string cachePath = Path.Combine(projectPath, "Intermediate", "Build", "Rules");
        Assembly assembly = new Assembler(ruleFiles, cachePath).Build();
        ModulesInfo info = new(configuration, platform);
        List<ModuleRules> modules = LoadModules(assembly, info, ruleFiles);
        if (!modules.Any(x => string.Equals(x.ModuleName, "Launch", StringComparison.OrdinalIgnoreCase)))
            throw new InvalidOperationException("Launch module not found. Create it with 'beast module Launch' or use 'beast new'.");

        var cpp = CreateCppEnvironment(platform, configuration, modules);
        cpp.IncludePaths.Add(Path.GetFullPath(sourcePath));
        string? shaders = await ShaderCompiler.BuildAsync(projectPath, platform.PlatformName, configuration, modules);
        if (shaders != null)
        {
            cpp.IncludePaths.Add(Path.GetDirectoryName(shaders)!);
            cpp.AdditionalPrerequisites.Add(shaders);
        }
        var link = CreateLinkEnvironment(platform, configuration, modules);
        if (BuildConfigManager.Config.Engine.Enabled)
        {
            string sdkSource = Path.Combine(EngineSdkBuilder.ResolveRoot(projectPath), "Source");
            cpp.IncludePaths.Add(sdkSource);
            cpp.AdditionalPrerequisites.AddRange(Directory.GetFiles(sdkSource, "*.h", SearchOption.AllDirectories));
            link.InputLibraries.Add(await EngineSdkBuilder.BuildAsync(projectPath, platform, configuration));
        }
        string objectPath = Path.Combine(projectPath, "Intermediate", "Build", platform.PlatformName, configuration.ToString(), "Objects");
        var sources = Directory.GetFiles(sourcePath, "*.*", SearchOption.AllDirectories)
            .Where(x => Path.GetExtension(x).ToLowerInvariant() is ".cpp" or ".c" or ".cc" or ".cxx")
            .ToList();
        if (sources.Count == 0) throw new InvalidOperationException("No C/C++ source files were found in Source.");

        var executor = new CommandExecutor(BuildConfigManager.Config.MaxParallelJobs);
        CPPOutput objects = platform.CompileCppFiles(executor, cpp, sources, objectPath);
        link.InputFiles.AddRange(objects.ObjectFiles);
        if (platform is WindowsPlatform windows)
            WindowsResourceCompiler.AddCompileCommand(executor, windows, projectPath, projectName, configuration, link);
        string outputPath = Path.Combine(projectPath, "Binaries", platform.PlatformName, configuration.ToString());
        Directory.CreateDirectory(outputPath);
        string baseName = platform.Type == PlatformType.Android ? "lib" + projectName : projectName;
        link.Output = platform.Type == PlatformType.Android ? LinkerOutput.SharedLibrary : LinkerOutput.Executable;
        platform.LinkFiles(executor, link, Path.Combine(outputPath, baseName));
        await executor.ExecuteAsync();

        if (BuildConfigManager.Config.Engine.Enabled)
        {
            string thirdParty = Path.Combine(EngineSdkBuilder.ResolveRoot(projectPath), "ThirdParty");
            string fontLicense = Path.Combine(thirdParty, "NotoSans", "LICENSE");
            string stbHeader = Path.Combine(thirdParty, "stb", "imstb_truetype.h");
            if (File.Exists(fontLicense) && File.Exists(stbHeader))
            {
                string stb = File.ReadAllText(stbHeader);
                int start = stb.LastIndexOf("ALTERNATIVE A - MIT License", StringComparison.Ordinal);
                int end = stb.LastIndexOf("ALTERNATIVE B - Public Domain", StringComparison.Ordinal);
                if (start < 0 || end < start) throw new InvalidOperationException("Cannot find stb license notices.");
                string notices = "Beast UI third-party notices\n\nNoto Sans Regular / Bold\n" + File.ReadAllText(fontLicense) +
                    "\n\nstb_truetype 1.26 (Dear ImGui variant; Omar Cornut and contributors)\n" + stb[start..end];
                string iconLicense = Path.Combine(thirdParty, "MaterialIcons", "LICENSE");
                if (File.Exists(iconLicense)) notices += "\n\nMaterial Design Icons, Google (Flutter subset)\n" + File.ReadAllText(iconLicense);
                string unicodeLicense = Path.Combine(thirdParty, "Unicode", "LICENSE");
                if (File.Exists(unicodeLicense)) notices += "\n\nUnicode 17.0.0 segmentation data\n" + File.ReadAllText(unicodeLicense);
                string shapingLicense = Path.Combine(thirdParty, "HarfBuzz", "LICENSE");
                if (File.Exists(shapingLicense)) notices += "\n\nHarfBuzz 12.3.0\n" + File.ReadAllText(shapingLicense);
                string fallbackLicense = Path.Combine(thirdParty, "NotoFallback", "LICENSE");
                if (File.Exists(fallbackLicense)) notices += "\n\nNoto Sans Devanagari Regular/Bold and Noto Sans Symbols 2\n" + File.ReadAllText(fallbackLicense);
                string bidiLicense = Path.Combine(thirdParty, "SheenBidi", "LICENSE");
                if (File.Exists(bidiLicense)) notices += "\n\nSheenBidi 3.0.0\n" + File.ReadAllText(bidiLicense);
                File.WriteAllText(Path.Combine(outputPath, "BeastSDK-LICENSES.txt"), notices);
                if (platform.Type == PlatformType.Android)
                {
                    string assets = Path.Combine(projectPath, "Platforms", "Android", "app", "src", "main", "assets");
                    Directory.CreateDirectory(assets);
                    File.WriteAllText(Path.Combine(assets, "BeastSDK-LICENSES.txt"), notices);
                }
            }
        }

        if (platform is AndroidPlatform android)
            await AndroidPackager.PackageAsync(projectPath, projectName, configuration, Path.Combine(outputPath, baseName + ".so"), android.NdkRoot);
    }

    internal static List<ModuleRules> LoadModules(Assembly assembly, ModulesInfo info, List<string> ruleFiles)
    {
        var result = new List<ModuleRules>();
        foreach (Type type in assembly.GetTypes().Where(x => x.IsClass && !x.IsAbstract && x.IsSubclassOf(typeof(ModuleRules))))
        {
            if (Activator.CreateInstance(type, info) is not ModuleRules rules) continue;
            rules.ModuleName = type.Name;
            rules.ModulePath = ruleFiles.First(x => Path.GetFileNameWithoutExtension(Path.GetFileNameWithoutExtension(x)).Equals(type.Name, StringComparison.OrdinalIgnoreCase));
            result.Add(rules);
        }
        return result;
    }

    internal static CPPEnvironment CreateCppEnvironment(Platform platform, TargetConfiguration configuration, IEnumerable<ModuleRules> modules)
    {
        var environment = new CPPEnvironment { CppVersion = CppVersion.Cpp20 };
        platform.SetupCPPEnvironment(environment);
        foreach (ModuleRules module in modules)
        {
            environment.IncludePaths.AddRange(module.PublicIncludePaths);
            environment.IncludePaths.AddRange(module.PrivateIncludePaths);
            environment.PreprocessorDefinitions.AddRange(module.Definitions);
            environment.AdditionalPrerequisites.Add(module.ModulePath);
        }
        if (configuration == TargetConfiguration.Debug)
        {
            environment.DebugInformation = true;
            environment.UseDebugCRT = platform.Type == PlatformType.Windows;
            environment.PreprocessorDefinitions.Add("DEBUG");
            environment.PreprocessorDefinitions.Add("_DEBUG");
        }
        else
        {
            environment.Optimization = true;
            environment.PreprocessorDefinitions.Add("NDEBUG");
        }
        return environment;
    }

    internal static LinkEnvironment CreateLinkEnvironment(Platform platform, TargetConfiguration configuration, IEnumerable<ModuleRules> modules)
    {
        var environment = new LinkEnvironment { DebugInformation = configuration == TargetConfiguration.Debug, Optimization = configuration != TargetConfiguration.Debug, UseIncrementalLinking = configuration == TargetConfiguration.Debug };
        platform.SetupLinkEnvironment(environment);
        foreach (ModuleRules module in modules)
        {
            environment.InputLibraries.AddRange(module.InputLibraries);
            environment.DelayLoadLibraries.AddRange(module.DelayLoadLibraries);
        }
        return environment;
    }
}

internal static class AndroidPackager
{
    public static async Task PackageAsync(string projectPath, string projectName, TargetConfiguration configuration, string nativeLibrary, string ndkRoot)
    {
        if (!File.Exists(nativeLibrary)) throw new FileNotFoundException("Android native library was not produced.", nativeLibrary);
        string androidProject = Path.Combine(projectPath, "Platforms", "Android");
        if (BuildConfigManager.Config.Engine.Enabled)
            EngineSdkBuilder.CopyAndroidPlatformSources(EngineSdkBuilder.ResolveRoot(projectPath), androidProject);
        string jniDirectory = Path.Combine(androidProject, "app", "src", "main", "jniLibs", "arm64-v8a");
        Directory.CreateDirectory(jniDirectory);
        File.Copy(nativeLibrary, Path.Combine(jniDirectory, "lib" + projectName + ".so"), true);

        // clang++ links against libc++_shared by default. NativeActivity loads the
        // project library directly, so the C++ runtime must be packaged beside it.
        var android = BuildConfigManager.Config.Android;
        string cxxRuntime = Path.Combine(ndkRoot, "toolchains", "llvm", "prebuilt", "windows-x86_64", "sysroot", "usr", "lib", "aarch64-linux-android", "libc++_shared.so");
        if (!File.Exists(cxxRuntime))
            throw new FileNotFoundException("Android NDK C++ shared runtime was not found.", cxxRuntime);
        File.Copy(cxxRuntime, Path.Combine(jniDirectory, "libc++_shared.so"), true);

        string symbolsDirectory = Path.Combine(projectPath, "Binaries", "Android", configuration.ToString(), "Symbols", "arm64-v8a");
        var symbolRecords = new List<object>();
        if (configuration == TargetConfiguration.Release)
        {
            string strip = Path.Combine(ndkRoot, "toolchains", "llvm", "prebuilt", "windows-x86_64", "bin", "llvm-strip.exe");
            if (!File.Exists(strip)) throw new FileNotFoundException("NDK llvm-strip is required for Release packaging.", strip);
            Directory.CreateDirectory(symbolsDirectory);
            foreach (string name in new[] { "lib" + projectName + ".so", "libc++_shared.so" })
            {
                string deployed = Path.Combine(jniDirectory, name);
                string original = Path.Combine(symbolsDirectory, name);
                File.Copy(deployed, original, true);
                var stripStart = new ProcessStartInfo(strip) { UseShellExecute = false, RedirectStandardOutput = true, RedirectStandardError = true };
                stripStart.ArgumentList.Add("--strip-unneeded");
                stripStart.ArgumentList.Add(deployed);
                using var stripProcess = Process.Start(stripStart) ?? throw new InvalidOperationException("Could not start llvm-strip.");
                var stripOut = stripProcess.StandardOutput.ReadToEndAsync();
                var stripErr = stripProcess.StandardError.ReadToEndAsync();
                await stripProcess.WaitForExitAsync();
                string stripLog = await stripOut + await stripErr;
                if (stripProcess.ExitCode != 0) throw new InvalidOperationException($"Stripping {name} failed: {stripLog}");
                symbolRecords.Add(new { library = name, originalSha256 = HashFile(original), packagedSha256 = HashFile(deployed) });
                Log.TraceInfo($"Release {name}: {new FileInfo(original).Length:N0} -> {new FileInfo(deployed).Length:N0} bytes; original preserved in Symbols.");
            }
        }

        string localProperties = Path.Combine(androidProject, "local.properties");
        File.WriteAllText(localProperties, $"sdk.dir={Escape(android.SdkRoot)}{Environment.NewLine}ndk.dir={Escape(ndkRoot)}{Environment.NewLine}");
        string gradlew = Path.Combine(androidProject, "gradlew.bat");
        if (!File.Exists(gradlew)) throw new FileNotFoundException("Android Gradle wrapper not found.", gradlew);
        string task = configuration == TargetConfiguration.Debug ? "assembleDebug" : "assembleRelease";
        Log.TraceInfo($"Packaging Android APK with Gradle task '{task}'...");
        var start = new ProcessStartInfo(gradlew, task)
        {
            WorkingDirectory = androidProject,
            UseShellExecute = false,
            RedirectStandardOutput = true,
            RedirectStandardError = true
        };
        start.Environment["JAVA_HOME"] = android.JavaHome;
        using var process = Process.Start(start) ?? throw new InvalidOperationException("Could not start Gradle.");
        Task<string> stdoutTask = process.StandardOutput.ReadToEndAsync();
        Task<string> stderrTask = process.StandardError.ReadToEndAsync();
        await process.WaitForExitAsync();
        string stdout = await stdoutTask;
        string stderr = await stderrTask;
        if (process.ExitCode != 0) throw new InvalidOperationException($"Gradle packaging failed:{Environment.NewLine}{stdout}{Environment.NewLine}{stderr}");
        string apkDirectory = Path.Combine(androidProject, "app", "build", "outputs", "apk", configuration == TargetConfiguration.Debug ? "debug" : "release");
        // Gradle chooses the name (unsigned release, signing config or custom
        // output naming). Never guess it or pick a stale APK from this directory.
        using var metadata = JsonDocument.Parse(File.ReadAllText(Path.Combine(apkDirectory, "output-metadata.json")));
        var elements = metadata.RootElement.GetProperty("elements");
        if (elements.GetArrayLength() != 1)
            throw new InvalidOperationException("Expected one APK output. Split APK packaging is not supported yet.");
        string fileName = elements[0].GetProperty("outputFile").GetString() ?? "";
        if (string.IsNullOrWhiteSpace(fileName) || Path.GetFileName(fileName) != fileName || !fileName.EndsWith(".apk", StringComparison.OrdinalIgnoreCase))
            throw new InvalidOperationException("Gradle returned an invalid APK output filename.");
        string apk = Path.Combine(apkDirectory, fileName);
        if (!File.Exists(apk)) throw new FileNotFoundException("Gradle APK output is missing.", apk);
        string output = Path.Combine(projectPath, "Binaries", "Android", configuration.ToString(), projectName + ".apk");
        Directory.CreateDirectory(Path.GetDirectoryName(output)!);
        await AndroidApkSigner.PublishAsync(projectPath, android, configuration, apk, output);
        if (configuration == TargetConfiguration.Release)
            File.WriteAllText(Path.Combine(symbolsDirectory, "manifest.json"), JsonSerializer.Serialize(new
            {
                apkSha256 = HashFile(output), abi = "arm64-v8a", ndk = Path.GetFileName(Path.TrimEndingDirectorySeparator(ndkRoot)),
                libraries = symbolRecords
            }, new JsonSerializerOptions { WriteIndented = true }));
        Log.TraceInfo($"Android APK: {output}");
    }

    private static string HashFile(string path)
    {
        using var stream = File.OpenRead(path);
        return Convert.ToHexString(System.Security.Cryptography.SHA256.HashData(stream));
    }
    private static string Escape(string path) => path.Replace("\\", "\\\\");
}
