using System.Security.Cryptography;
using System.Text;

namespace Beast.BuildTool;

/// <summary>Builds SDK components as one library, independent of application Build.cs rules.</summary>
public static class EngineSdkBuilder
{
    internal static void CopyAndroidPlatformSources(string sdk, string gradleRoot)
    {
        string target = Path.Combine(gradleRoot, "app", "src", "main", "java", "com", "beast", "sdk");
        Directory.CreateDirectory(target);
        File.Copy(Path.Combine(sdk, "Platform", "Android", "BeastTextInput.java"), Path.Combine(target, "BeastTextInput.java"), true);
        File.Copy(Path.Combine(sdk, "Platform", "Android", "BeastAccessibility.java"), Path.Combine(target, "BeastAccessibility.java"), true);
    }

    public static string ResolveRoot(string projectDir)
    {
        string configured = BuildConfigManager.Config.Engine.SdkRoot;
        if (!string.IsNullOrWhiteSpace(configured))
        {
            string root = Path.GetFullPath(Environment.ExpandEnvironmentVariables(configured), projectDir);
            if (!File.Exists(Path.Combine(root, "Source", "Beast.h")))
                throw new DirectoryNotFoundException($"Beast SDK header not found: {root}/Source/Beast.h");
            return root;
        }
        string[] candidates = [Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, "..", "..", "BeastSDK")),
            Path.Combine(AppContext.BaseDirectory, "BeastSDK")];
        return candidates.FirstOrDefault(path => File.Exists(Path.Combine(path, "Source", "Beast.h")))
            ?? throw new DirectoryNotFoundException("BeastSDK was not found. Set engine.sdkRoot in Config/BuildConfig.json.");
    }

    public static string GetLibraryPath(string projectDir, PlatformType platform, TargetConfiguration config) =>
        Path.Combine(projectDir, "Intermediate", "Build", "BeastSDK", platform.ToString(), config.ToString(),
            platform == PlatformType.Windows ? "Beast.lib" : "libBeast.a");

    public static async Task<string> BuildAsync(string projectDir, Platform platform, TargetConfiguration configuration)
    {
        string root = ResolveRoot(projectDir);
        string source = Path.Combine(root, "Source");
        string output = GetLibraryPath(projectDir, platform.Type, configuration);
        string cache = Path.GetDirectoryName(output)!;
        Directory.CreateDirectory(cache);
        var sources = Directory.GetFiles(source, "*", SearchOption.AllDirectories).Where(path => Path.GetExtension(path) is ".cpp" or ".c")
            .Where(path => !Path.GetRelativePath(source, path).StartsWith("Platform" + Path.DirectorySeparatorChar) ||
                path.StartsWith(Path.Combine(source, "Platform", platform.PlatformName) + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase))
            .Order(StringComparer.OrdinalIgnoreCase).ToList();
        var cpp = new CPPEnvironment { CppVersion = CppVersion.Cpp20,
            DebugInformation = configuration == TargetConfiguration.Debug,
            Optimization = configuration != TargetConfiguration.Debug,
            UseDebugCRT = platform.Type == PlatformType.Windows && configuration == TargetConfiguration.Debug };
        platform.SetupCPPEnvironment(cpp);
        cpp.IncludePaths.Add(source);
        cpp.IncludePaths.Add(Path.Combine(root, "ThirdParty", "SheenBidi", "Headers"));
        cpp.IncludePaths.Add(Path.Combine(root, "ThirdParty", "SheenBidi", "Source"));
        cpp.AdditionalPrerequisites.AddRange(Directory.GetFiles(Path.Combine(root, "ThirdParty", "SheenBidi"), "*", SearchOption.AllDirectories));
        cpp.IncludePaths.Add(Path.Combine(root, "ThirdParty", "OpenGL"));
        cpp.IncludePaths.Add(Path.Combine(root, "ThirdParty", "Vulkan-Headers", "include"));
        cpp.PreprocessorDefinitions.Add("BEAST_STATIC=1");
        cpp.PreprocessorDefinitions.Add(configuration == TargetConfiguration.Debug ? "_DEBUG" : "NDEBUG");
        cpp.AdditionalPrerequisites.AddRange(Directory.GetFiles(source, "*.h", SearchOption.AllDirectories));
        cpp.AdditionalPrerequisites.AddRange(Directory.GetFiles(Path.Combine(root, "ThirdParty", "HarfBuzz", "src"), "*", SearchOption.AllDirectories)
            .Where(path => Path.GetExtension(path) is ".h" or ".hh" or ".cc"));
        cpp.AdditionalPrerequisites.AddRange(Directory.GetFiles(Path.Combine(root, "ThirdParty", "OpenGL"), "*.h", SearchOption.AllDirectories));
        cpp.AdditionalPrerequisites.AddRange(Directory.GetFiles(Path.Combine(root, "ThirdParty", "Vulkan-Headers", "include"), "*", SearchOption.AllDirectories)
            .Where(path => Path.GetExtension(path) is ".h" or ".hpp"));
        string fontRasterizer = Path.Combine(root, "ThirdParty", "stb", "imstb_truetype.h");
        if (File.Exists(fontRasterizer)) cpp.AdditionalPrerequisites.Add(fontRasterizer);
        if (platform is AndroidPlatform androidPlatform)
            cpp.IncludePaths.Add(Path.Combine(androidPlatform.NdkRoot, "sources", "android", "native_app_glue"));
        var executor = new CommandExecutor(BuildConfigManager.Config.MaxParallelJobs);
        CPPOutput compiled = platform.CompileCppFiles(executor, cpp, sources, Path.Combine(cache, "Objects"));
        if (platform is AndroidPlatform android)
        {
            string glue = Path.Combine(android.NdkRoot, "sources", "android", "native_app_glue");
            var glueCpp = new CPPEnvironment();
            platform.SetupCPPEnvironment(glueCpp);
            glueCpp.PreprocessorDefinitions.Add("ANativeActivity_onCreate=BeastNativeActivityOnCreate");
            glueCpp.AdditionalPrerequisites.Add(Path.Combine(glue, "android_native_app_glue.h"));
            compiled.ObjectFiles.AddRange(platform.CompileCppFiles(executor, glueCpp,
                [Path.Combine(glue, "android_native_app_glue.c")], Path.Combine(cache, "Glue")).ObjectFiles);
        }

        // Removed sources must not survive as stale archive members.
        string manifest = Path.Combine(cache, "archive-inputs.txt");
        string fingerprint = Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(root + "\n" + string.Join("\n", compiled.ObjectFiles))));
        if (!File.Exists(manifest) || File.ReadAllText(manifest) != fingerprint)
        {
            if (File.Exists(output)) File.Delete(output);
            File.WriteAllText(manifest, fingerprint);
        }
        platform.ArchiveFiles(executor, compiled.ObjectFiles, output);
        await executor.ExecuteAsync();
        Log.TraceInfo($"Beast SDK: {output}");
        return output;
    }
}
