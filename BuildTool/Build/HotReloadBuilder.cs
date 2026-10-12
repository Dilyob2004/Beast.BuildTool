using System.Diagnostics;
using System.Security.Cryptography;
using System.Text;
using System.Text.RegularExpressions;
using System.Xml.Linq;

namespace Beast.BuildTool;

// A watch session has its own immutable runtime and versioned module files.
// Ordinary build/package outputs and the production application's data are untouched.
internal sealed class HotReloadBuilder
{
    internal readonly string Project, Name, Root, Cache, Sdk, Fingerprint;
    internal readonly Platform Platform;
    internal readonly List<ModuleRules> Modules;
    internal readonly string[] IncludeRoots;
    internal string RuntimeLink = "";
    private readonly string baseline;
    internal HotReloadBuilder(string project, Platform platform)
    {
        Project = project; Name = new DirectoryInfo(project).Name; Platform = platform;
        if (!Regex.IsMatch(Name, "^[A-Za-z_][A-Za-z0-9_]*$")) throw new ArgumentException("Hot reload requires a portable project name.");
        if (!BuildConfigManager.Config.Engine.Enabled) throw new InvalidOperationException("Hot reload requires Beast SDK and BEAST_UI_APPLICATION.");
        Sdk = EngineSdkBuilder.ResolveRoot(project);
        if (platform is WindowsPlatform) WindowsTemplateGenerator.Ensure(project, Name);
        string session = Guid.NewGuid().ToString("N");
        Root = Path.Combine(project, "Binaries", platform.PlatformName, "HotReload", session);
        Cache = Path.Combine(project, "Intermediate", "Build", "HotReload", platform.PlatformName, session);
        Directory.CreateDirectory(Root); Directory.CreateDirectory(Cache);
        var rules = Directory.GetFiles(Path.Combine(project, "Source"), "*.Build.cs", SearchOption.AllDirectories).ToList();
        var assembly = new Assembler(rules, Path.Combine(Cache, "Rules")).Build();
        Modules = GenericProjectBuilder.LoadModules(assembly, new ModulesInfo(TargetConfiguration.Debug, platform), rules);
        if (!Modules.Any(m => m.ModuleName == "Launch")) throw new InvalidOperationException("Launch module is required.");
        IncludeRoots = Modules.SelectMany(m => m.PublicIncludePaths.Concat(m.PrivateIncludePaths))
            .Select(p => Path.GetFullPath(p, project)).Where(Directory.Exists).Append(Path.Combine(project, "Source")).Distinct(StringComparer.OrdinalIgnoreCase).ToArray();
        baseline = InfrastructureStamp();
        Fingerprint = HashFiles(InfrastructureFiles().Concat(Directory.GetFiles(Path.Combine(Sdk, "Source"), "*", SearchOption.AllDirectories)),
            platform.PlatformName + platform.Architecture + "Debug/hot-v1/" + (platform is AndroidPlatform a ? a.ToolchainRoot : ((WindowsPlatform)platform).DumpBinPath));
    }
    private IEnumerable<string> InfrastructureFiles() => Directory.GetFiles(Path.Combine(Project, "Config"), "*", SearchOption.AllDirectories)
        .Concat(Directory.GetFiles(Path.Combine(Project, "Source"), "*.Build.cs", SearchOption.AllDirectories))
        .Concat(Directory.GetFiles(Path.Combine(AppContext.BaseDirectory, "Templates", "HotReload"), "*", SearchOption.AllDirectories))
        .Concat(Platform is WindowsPlatform ? Directory.GetFiles(Path.Combine(Project, "Platforms", "Windows", "Resources"), "*", SearchOption.AllDirectories) : [])
        .Concat(Platform is AndroidPlatform a ? new[] { Path.Combine(a.NdkRoot, "source.properties") } :
            new[] { Path.Combine(Path.GetDirectoryName(((WindowsPlatform)Platform).DumpBinPath)!, "cl.exe"), Path.Combine(Path.GetDirectoryName(((WindowsPlatform)Platform).DumpBinPath)!, "link.exe") })
        .Append(typeof(HotReloadBuilder).Assembly.Location);
    private string InfrastructureStamp()
    {
        // Avoid hashing all Vulkan headers/fonts twice a second. Metadata changes
        // require a fresh session; the actual ABI token hashes SDK source content.
        var entries = InfrastructureFiles().Concat(Directory.GetFiles(Sdk, "*", SearchOption.AllDirectories))
            .Concat(Modules.SelectMany(m => m.InputLibraries).Where(File.Exists))
            .Distinct(StringComparer.OrdinalIgnoreCase).Order(StringComparer.OrdinalIgnoreCase)
            .Select(path => { var file = new FileInfo(path); return path + "|" + file.Length + "|" + file.LastWriteTimeUtc.Ticks; });
        return Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(string.Join('\n', entries))));
    }
    internal void CheckInfrastructure()
    {
        if (InfrastructureStamp() != baseline) throw new InvalidOperationException("SDK, Build.cs, configuration or BuildTool changed. Restart beast watch; the running host is unchanged.");
    }
    private IEnumerable<string> SourceFiles(string root)
    {
        foreach (string file in Directory.EnumerateFiles(root)) yield return file;
        foreach (string directory in Directory.EnumerateDirectories(root))
        {
            if ((File.GetAttributes(directory) & FileAttributes.ReparsePoint) != 0 ||
                new[] { "Binaries", "Intermediate", ".git", ".vs", ".gradle" }.Contains(Path.GetFileName(directory), StringComparer.OrdinalIgnoreCase)) continue;
            foreach (string file in SourceFiles(directory)) yield return file;
        }
    }
    internal string SourceStamp() => HashFiles(IncludeRoots.SelectMany(SourceFiles)
        .Where(p => new[] { ".cpp", ".cc", ".cxx", ".c", ".h", ".hpp", ".inl", ".hlsl", ".glsl" }.Contains(Path.GetExtension(p).ToLowerInvariant())), "");
    private static string HashFiles(IEnumerable<string> files, string salt)
    {
        using var hash = IncrementalHash.CreateHash(HashAlgorithmName.SHA256);
        hash.AppendData(Encoding.UTF8.GetBytes(salt));
        foreach (string path in files.Distinct(StringComparer.OrdinalIgnoreCase).Order(StringComparer.OrdinalIgnoreCase))
        {
            hash.AppendData(Encoding.UTF8.GetBytes(path));
            using var file = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete);
            hash.AppendData(SHA256.HashData(file));
        }
        return Convert.ToHexString(hash.GetHashAndReset());
    }
    private CPPEnvironment Cpp(bool sdk = false)
    {
        var cpp = GenericProjectBuilder.CreateCppEnvironment(Platform, TargetConfiguration.Debug, sdk ? [] : Modules);
        cpp.IncludePaths.Add(Path.Combine(Sdk, "Source"));
        cpp.AdditionalPrerequisites.AddRange(Directory.GetFiles(Path.Combine(Sdk, "Source"), "*.h", SearchOption.AllDirectories));
        cpp.PreprocessorDefinitions.Add("BEAST_HOT_ABI=\\\"" + Fingerprint + "\\\"");
        return cpp;
    }
    private LinkEnvironment Link()
    {
        var link = GenericProjectBuilder.CreateLinkEnvironment(Platform, TargetConfiguration.Debug, []);
        link.UseIncrementalLinking = false; link.Output = LinkerOutput.SharedLibrary;
        return link;
    }
    internal async Task BuildRuntime()
    {
        var cpp = Cpp(true); cpp.PreprocessorDefinitions.Add("BEAST_HOT_HOST=1");
        cpp.IncludePaths.Add(Path.Combine(Sdk, "ThirdParty", "OpenGL"));
        cpp.IncludePaths.Add(Path.Combine(Sdk, "ThirdParty", "Vulkan-Headers", "include"));
        if (Platform is AndroidPlatform a) cpp.IncludePaths.Add(Path.Combine(a.NdkRoot, "sources", "android", "native_app_glue"));
        string source = Path.Combine(Sdk, "Source");
        cpp.IncludePaths.Add(Path.Combine(Sdk, "ThirdParty", "SheenBidi", "Headers"));
        cpp.IncludePaths.Add(Path.Combine(Sdk, "ThirdParty", "SheenBidi", "Source"));
        cpp.AdditionalPrerequisites.AddRange(Directory.GetFiles(Path.Combine(Sdk, "ThirdParty", "SheenBidi"), "*", SearchOption.AllDirectories));
        cpp.AdditionalPrerequisites.AddRange(Directory.GetFiles(Path.Combine(Sdk, "ThirdParty", "HarfBuzz", "src"), "*", SearchOption.AllDirectories)
            .Where(path => Path.GetExtension(path) is ".h" or ".hh" or ".cc"));
        var sources = Directory.GetFiles(source, "*", SearchOption.AllDirectories).Where(p => Path.GetExtension(p) is ".cpp" or ".c").Where(p =>
            !Path.GetRelativePath(source, p).StartsWith("Platform" + Path.DirectorySeparatorChar) ||
            p.StartsWith(Path.Combine(source, "Platform", Platform.PlatformName) + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase)).ToList();
        var executor = new CommandExecutor(BuildConfigManager.Config.MaxParallelJobs);
        var compiled = Platform.CompileCppFiles(executor, cpp, sources, Path.Combine(Cache, "SdkObjects"));
        await executor.ExecuteAsync();
        var link = Link(); link.InputFiles.AddRange(compiled.ObjectFiles);
        if (Platform is WindowsPlatform windows)
        {
            // Export defined Beast functions from the same-toolchain SDK. No
            // static SDK archive is ever linked into a project module or host.
            var exports = new SortedSet<string>(StringComparer.Ordinal);
            foreach (var obj in compiled.ObjectFiles)
            {
                string dump = await Execute(windows.DumpBinPath, ["/symbols", obj]);
                foreach (var line in dump.Split('\n'))
                {
                    if (!line.Contains("External") || !line.Contains("()") || line.Contains("UNDEF")) continue;
                    var match = Regex.Match(line, @"\|\s+(\?\S+)");
                    if (match.Success && match.Groups[1].Value.Contains("@Beast")) exports.Add(match.Groups[1].Value);
                }
            }
            if (exports.Count == 0) throw new InvalidOperationException("No SDK exports found.");
            string def = Path.Combine(Cache, "BeastRuntime.def");
            File.WriteAllLines(def, new[] { "EXPORTS" }.Concat(exports.Select(e => "  " + e)));
            link.CustomArgs.Add("/DEF:\"" + def + "\"");
        }
        else link.CustomArgs.Add("-Wl,-soname,libBeastRuntime.so");
        string runtime = Path.Combine(Root, Platform.Type == PlatformType.Windows ? "BeastRuntime" : "libBeastRuntime");
        executor = new CommandExecutor(BuildConfigManager.Config.MaxParallelJobs);
        Platform.LinkFiles(executor, link, runtime); await executor.ExecuteAsync();
        RuntimeLink = runtime + (Platform.Type == PlatformType.Windows ? ".lib" : ".so");
        var notices = new StringBuilder("Beast SDK third-party notices\n\n");
        foreach (string component in new[] { "NotoSans", "MaterialIcons", "Unicode", "HarfBuzz", "NotoFallback", "SheenBidi" }) {
            string license = Path.Combine(Sdk, "ThirdParty", component, "LICENSE");
            if (File.Exists(license)) notices.AppendLine(component).AppendLine(File.ReadAllText(license));
        }
        string stb = File.ReadAllText(Path.Combine(Sdk, "ThirdParty", "stb", "imstb_truetype.h"));
        int licenseStart = stb.LastIndexOf("ALTERNATIVE A - MIT License", StringComparison.Ordinal);
        int licenseEnd = stb.LastIndexOf("ALTERNATIVE B - Public Domain", StringComparison.Ordinal);
        if (licenseStart < 0 || licenseEnd < licenseStart) throw new InvalidOperationException("stb license notice missing.");
        notices.AppendLine("stb_truetype 1.26 (Dear ImGui variant)").AppendLine(stb[licenseStart..licenseEnd]);
        File.WriteAllText(Path.Combine(Root, "BeastSDK-LICENSES.txt"), notices.ToString());
    }
    internal async Task<string> BuildModule(string generation)
    {
        CheckInfrastructure();
        var cpp = Cpp(); cpp.PreprocessorDefinitions.Add("BEAST_HOT_MODULE=1");
        cpp.IncludePaths.AddRange(IncludeRoots);
        cpp.AdditionalPrerequisites.AddRange(IncludeRoots.SelectMany(SourceFiles)
            .Where(p => new[] { ".h", ".hpp", ".inl" }.Contains(Path.GetExtension(p).ToLowerInvariant())));
        string? shaders = await ShaderCompiler.BuildAsync(Project, Platform.PlatformName, TargetConfiguration.Debug, Modules);
        if (shaders != null) { cpp.IncludePaths.Add(Path.GetDirectoryName(shaders)!); cpp.AdditionalPrerequisites.Add(shaders); }
        var sources = SourceFiles(Path.Combine(Project, "Source"))
            .Where(p => new[] { ".cpp", ".cc", ".cxx", ".c" }.Contains(Path.GetExtension(p).ToLowerInvariant())).ToList();
        var executor = new CommandExecutor(BuildConfigManager.Config.MaxParallelJobs);
        var compiled = Platform.CompileCppFiles(executor, cpp, sources, Path.Combine(Cache, "ProjectObjects"));
        var link = GenericProjectBuilder.CreateLinkEnvironment(Platform, TargetConfiguration.Debug, Modules);
        link.Output = LinkerOutput.SharedLibrary; link.UseIncrementalLinking = false;
        link.InputFiles.AddRange(compiled.ObjectFiles); link.InputLibraries.Add(RuntimeLink);
        string name = (Platform.Type == PlatformType.Android ? "lib" : "") + "BeastModule_" + generation;
        if (Platform.Type == PlatformType.Android) { link.CustomArgs.Add("-Wl,-soname," + name + ".so"); link.CustomArgs.Add("-Wl,-Bsymbolic"); }
        Platform.LinkFiles(executor, link, Path.Combine(Root, name)); await executor.ExecuteAsync();
        CheckInfrastructure();
        return Path.Combine(Root, name + Platform.SharedLibraryFileExtension);
    }
    internal async Task BuildHost()
    {
        var cpp = Cpp(true);
        string template = Path.Combine(AppContext.BaseDirectory, "Templates", "HotReload", "Host.cpp");
        var executor = new CommandExecutor(BuildConfigManager.Config.MaxParallelJobs);
        if (Platform is AndroidPlatform a) cpp.IncludePaths.Add(Path.Combine(a.NdkRoot, "sources", "android", "native_app_glue"));
        var compiled = Platform.CompileCppFiles(executor, cpp, [template], Path.Combine(Cache, "HostObjects"));
        if (Platform is AndroidPlatform android)
        {
            var glueCpp = Cpp(true); glueCpp.PreprocessorDefinitions.Add("ANativeActivity_onCreate=BeastNativeActivityOnCreate");
            compiled.ObjectFiles.AddRange(Platform.CompileCppFiles(executor, glueCpp,
                [Path.Combine(android.NdkRoot, "sources", "android", "native_app_glue", "android_native_app_glue.c")], Path.Combine(Cache, "Glue")).ObjectFiles);
        }
        var link = Link(); link.InputFiles.AddRange(compiled.ObjectFiles); link.InputLibraries.Add(RuntimeLink);
        if (Platform is WindowsPlatform windows)
        {
            link.Output = LinkerOutput.Executable;
            // The process manifest belongs to the EXE, not to the reloadable DLL.
            // Match ordinary builds, including windows.dpiAware and the app icon.
            WindowsResourceCompiler.AddCompileCommand(executor, windows, Project, Name,
                TargetConfiguration.Debug, link, Path.Combine(Cache, "HostResources"));
        }
        Platform.LinkFiles(executor, link, Path.Combine(Root, Platform.Type == PlatformType.Windows ? "BeastHost" : "lib" + Name));
        await executor.ExecuteAsync();
    }
    internal async Task<string> PackageHost()
    {
        var android = (AndroidPlatform)Platform;
        string gradle = Path.Combine(Cache, "Android");
        string original = Path.Combine(Project, "Platforms", "Android");
        foreach (string file in Directory.GetFiles(original, "*", SearchOption.AllDirectories))
        {
            string relative = Path.GetRelativePath(original, file);
            if (relative.Split(Path.DirectorySeparatorChar).Any(s => s is "build" or ".gradle" or "jniLibs" or ".cxx")) continue;
            string target = Path.Combine(gradle, relative); Directory.CreateDirectory(Path.GetDirectoryName(target)!); File.Copy(file, target);
        }
        EngineSdkBuilder.CopyAndroidPlatformSources(Sdk, gradle);
        string script = Path.Combine(gradle, "app", "build.gradle");
        string text = File.ReadAllText(script);
        var id = Regex.Match(text, "applicationId\\s+[\"']([^\"']+)[\"']");
        if (!id.Success) throw new InvalidOperationException("Cannot determine Gradle applicationId for dev host.");
        string originalId = id.Groups[1].Value;
        var namespaceMatch = Regex.Match(text, "namespace\\s+[\"']([^\"']+)[\"']");
        string activityNamespace = namespaceMatch.Success ? namespaceMatch.Groups[1].Value : originalId;
        string package = "com.beast." + Name.ToLowerInvariant() + ".hot";
        text = text.Replace(id.Value, "applicationId '" + package + "'").Replace("arm64-v8a", android.Abi);
        // The copied project is explicitly debug-only, including user projects
        // which had disabled debuggable in their normal configuration.
        text += "\nandroid.buildTypes.debug.debuggable = true\n";
        File.WriteAllText(script, text);
        string manifestPath = Path.Combine(gradle, "app", "src", "main", "AndroidManifest.xml");
        var manifest = XDocument.Load(manifestPath); XNamespace ns = "http://schemas.android.com/apk/res/android";
        var app = manifest.Root!.Element("application")!; app.SetAttributeValue(ns + "allowBackup", "false");
        foreach (var element in app.Elements("activity"))
        {
            string? name = (string?)element.Attribute(ns + "name");
            if (name?.StartsWith('.') == true) element.SetAttributeValue(ns + "name", activityNamespace + name);
        }
        manifest.Save(manifestPath);
        string jni = Path.Combine(gradle, "app", "src", "main", "jniLibs", android.Abi); Directory.CreateDirectory(jni);
        foreach (string name in new[] { "lib" + Name + ".so", "libBeastRuntime.so", "libBeastModule_initial.so" }) File.Copy(Path.Combine(Root, name), Path.Combine(jni, name));
        File.Copy(Path.Combine(android.ToolchainRoot, "sysroot", "usr", "lib", android.Architecture == PlatformArch.X64 ? "x86_64-linux-android" : "aarch64-linux-android", "libc++_shared.so"), Path.Combine(jni, "libc++_shared.so"));
        File.WriteAllText(Path.Combine(gradle, "local.properties"), "sdk.dir=" + BuildConfigManager.Config.Android.SdkRoot.Replace("\\", "/") + "\n");
        string assets = Path.Combine(gradle, "app", "src", "main", "assets"); Directory.CreateDirectory(assets);
        File.Copy(Path.Combine(Root, "BeastSDK-LICENSES.txt"), Path.Combine(assets, "BeastSDK-LICENSES.txt"), true);
        await Execute(Path.Combine(gradle, "gradlew.bat"), ["assembleDebug"], gradle, true);
        string apk = Path.Combine(Root, Name + ".HotReload.apk");
        await AndroidApkSigner.PublishAsync(Project, BuildConfigManager.Config.Android, TargetConfiguration.Debug,
            Path.Combine(gradle, "app", "build", "outputs", "apk", "debug", "app-debug.apk"), apk);
        return activityNamespace; // Qualified GameActivity class; package has the .hot suffix.
    }
    internal static async Task<string> Execute(string executable, IEnumerable<string> arguments, string? cwd = null, bool java = false)
    {
        var start = new ProcessStartInfo(executable) { UseShellExecute = false, RedirectStandardOutput = true, RedirectStandardError = true, CreateNoWindow = true };
        if (cwd != null) start.WorkingDirectory = cwd;
        if (java) start.Environment["JAVA_HOME"] = BuildConfigManager.Config.Android.JavaHome;
        foreach (string arg in arguments) start.ArgumentList.Add(arg);
        using var process = Process.Start(start) ?? throw new InvalidOperationException("Could not start " + executable);
        var stdout = process.StandardOutput.ReadToEndAsync(); var stderr = process.StandardError.ReadToEndAsync();
        await process.WaitForExitAsync(); string output = await stdout + await stderr;
        if (process.ExitCode != 0) throw new InvalidOperationException(Path.GetFileName(executable) + " failed: " + output);
        return output;
    }
}
