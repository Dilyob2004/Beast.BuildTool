using System.Text.Json;
using System.Text.Json.Serialization;

namespace Beast.BuildTool;

/// <summary>Per-project toolchain settings stored in Config/BuildConfig.json.</summary>
public sealed class BuildConfig
{
    /// <summary>Visual Studio Windows launcher: Debug uses watch when enabled.</summary>
    [JsonPropertyName("hotReload")]
    public bool HotReload { get; set; } = false;

    [JsonPropertyName("shaderTools")]
    public ShaderToolsConfig ShaderTools { get; set; } = new();
    [JsonPropertyName("engine")]
    public EngineBuildConfig Engine { get; set; } = new();
    [JsonPropertyName("defaultConfiguration")]
    public string DefaultConfiguration { get; set; } = "Debug";

    [JsonPropertyName("defaultPlatform")]
    public string DefaultPlatform { get; set; } = "Windows";

    [JsonPropertyName("maxParallelJobs")]
    public int MaxParallelJobs { get; set; }

    [JsonPropertyName("android")]
    public AndroidBuildConfig Android { get; set; } = new();

    [JsonPropertyName("windows")]
    public WindowsBuildConfig Windows { get; set; } = new();
}

public sealed class ShaderToolsConfig
{
    [JsonPropertyName("dxc")]
    public string Dxc { get; set; } = "";
    [JsonPropertyName("spirvCross")]
    public string SpirvCross { get; set; } = "";
    [JsonPropertyName("spirvValidator")]
    public string SpirvValidator { get; set; } = "";
}

public sealed class EngineBuildConfig
{
    [JsonPropertyName("enabled")]
    public bool Enabled { get; set; } = true;

    /// <summary>Optional SDK root (containing Source/Beast.h), relative to project or absolute.</summary>
    [JsonPropertyName("sdkRoot")]
    public string SdkRoot { get; set; } = "";
}

public sealed class WindowsBuildConfig
{
    /// <summary>Embed the per-monitor DPI-aware Windows manifest in the executable.</summary>
    [JsonPropertyName("dpiAware")]
    public bool DpiAware { get; set; } = true;
}

public sealed class AndroidBuildConfig
{
    [JsonPropertyName("releaseSigning")]
    public AndroidSigningConfig ReleaseSigning { get; set; } = new();

    [JsonPropertyName("javaHome")]
    public string JavaHome { get; set; } = @"C:\Program Files\Android\Android Studio\jbr";

    [JsonPropertyName("sdkRoot")]
    public string SdkRoot { get; set; } = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Android", "Sdk");

    /// <summary>Either an NDK directory or its parent directory containing versions.</summary>
    [JsonPropertyName("ndkRoot")]
    public string NdkRoot { get; set; } = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Android", "Sdk", "ndk");

    [JsonPropertyName("ndkVersion")]
    public string NdkVersion { get; set; } = "";

    [JsonPropertyName("minSdk")]
    public int MinSdk { get; set; } = 29;

    [JsonPropertyName("targetSdk")]
    public int TargetSdk { get; set; } = 35;

    /// <summary>AVD name used when no Android emulator is already running.</summary>
    [JsonPropertyName("defaultAvd")]
    public string DefaultAvd { get; set; } = "";

    [JsonPropertyName("emulatorBootTimeoutSeconds")]
    public int EmulatorBootTimeoutSeconds { get; set; } = 180;
}

public sealed class AndroidSigningConfig
{
    /// <summary>Empty: preserve Gradle signing, or use the local Android debug key.</summary>
    [JsonPropertyName("keyStore")]
    public string KeyStore { get; set; } = "";
    [JsonPropertyName("keyAlias")]
    public string KeyAlias { get; set; } = "";
    [JsonPropertyName("storePasswordEnv")]
    public string StorePasswordEnv { get; set; } = "";
    [JsonPropertyName("keyPasswordEnv")]
    public string KeyPasswordEnv { get; set; } = "";
}

public static class BuildConfigManager
{
    private static BuildConfig _config = new();
    public static BuildConfig Config => _config;

    public static void LoadConfig(string projectPath)
    {
        string configPath = Path.Combine(projectPath, "Config", "BuildConfig.json");
        if (!File.Exists(configPath))
        {
            _config = new BuildConfig();
            return;
        }

        try
        {
            _config = JsonSerializer.Deserialize<BuildConfig>(File.ReadAllText(configPath), JsonOptions) ?? new BuildConfig();
        }
        catch (Exception ex)
        {
            // Falling back here can silently replace a configured release
            // signing identity with the default debug key.
            throw new InvalidOperationException($"Cannot read '{configPath}'. Fix the build configuration before building.", ex);
        }
    }

    public static void CreateDefault(string projectPath)
    {
        string configPath = Path.Combine(projectPath, "Config", "BuildConfig.json");
        Directory.CreateDirectory(Path.GetDirectoryName(configPath)!);
        File.WriteAllText(configPath, JsonSerializer.Serialize(new BuildConfig(), JsonOptions));
    }

    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        WriteIndented = true,
        PropertyNameCaseInsensitive = true,
        ReadCommentHandling = JsonCommentHandling.Skip,
        AllowTrailingCommas = true
    };
}
