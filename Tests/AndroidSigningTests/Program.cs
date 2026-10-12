using Beast.BuildTool;
using System.Reflection;
using System.Security.Cryptography;
using System.Text.Json;

string repository = Path.GetFullPath(args.Length > 0 ? args[0] : ".");
string project = Path.Combine(repository, "Test", "EcoSbor");
BuildConfigManager.LoadConfig(project);
var android = BuildConfigManager.Config.Android;
string unsigned = Path.Combine(project, "Platforms", "Android", "app", "build", "outputs", "apk", "release", "app-release-unsigned.apk");
if (!File.Exists(unsigned)) throw new Exception("Build EcoSbor Android Release first.");
string work = Path.Combine(project, "Intermediate", "Verification", "SigningTests-" + Guid.NewGuid().ToString("N"));
Directory.CreateDirectory(work);
string output = Path.Combine(work, "signed.apk");
var publish = typeof(BuildConfig).Assembly.GetType("Beast.BuildTool.AndroidApkSigner")!.GetMethod("PublishAsync")!;
async Task Sign(string input, AndroidSigningConfig? signing = null, TargetConfiguration configuration = TargetConfiguration.Release)
{
    android.ReleaseSigning = signing ?? new();
    await (Task)publish.Invoke(null, [project, android, configuration, input, output])!;
}
static string Hash(string path) => Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(path)));
static void Check(bool value, string reason) { if (!value) throw new Exception(reason); }
async Task Reject(AndroidSigningConfig config)
{
    string previous = Hash(output); bool rejected = false;
    try { await Sign(unsigned, config); } catch (InvalidOperationException) { rejected = true; }
    catch (FileNotFoundException) { rejected = true; }
    Check(rejected && Hash(output) == previous, "Invalid signing config must fail and preserve the published APK.");
    Check(!Directory.GetFiles(work, ".beast-signing-*.apk").Any(), "Leaked temporary APK.");
}

string? savedHome = Environment.GetEnvironmentVariable("ANDROID_USER_HOME");
try
{
    // Never touch the developer's signing identity. Exercise first-use creation
    // and repeat builds with a disposable, explicitly scoped Android user home.
    Environment.SetEnvironmentVariable("ANDROID_USER_HOME", Path.Combine(work, "android-user"));
    await Sign(unsigned);
    string key = Path.Combine(work, "android-user", "debug.keystore");
    string keyHash = Hash(key), signedHash = Hash(output);
    string signedInput = Path.Combine(work, "gradle-signed.apk"); File.Copy(output, signedInput);
    await Sign(signedInput);
    Check(Hash(output) == signedHash, "An existing Gradle signature must be preserved byte-for-byte.");
    await Sign(signedInput, new() { KeyStore = "missing-release-key.jks" }, TargetConfiguration.Debug);
    Check(Hash(output) == signedHash, "Release key configuration must not replace the Debug signing identity.");
    await Sign(unsigned); Check(Hash(key) == keyHash, "Default key must be stable across builds.");

    Environment.SetEnvironmentVariable("BEAST_SIGNING_TEST_STORE", "android");
    Environment.SetEnvironmentVariable("BEAST_SIGNING_TEST_KEY", "android");
    var custom = new AndroidSigningConfig {
        KeyStore = Path.GetRelativePath(project, key), KeyAlias = "androiddebugkey",
        StorePasswordEnv = "BEAST_SIGNING_TEST_STORE", KeyPasswordEnv = "BEAST_SIGNING_TEST_KEY"
    };
    await Sign(unsigned, custom);
    await Reject(new() { KeyAlias = "partial" });
    await Reject(new() { KeyStore = Path.Combine(work, "missing.jks"), KeyAlias = "missing" });
    await Reject(new() { KeyStore = key });
    await Reject(new() { KeyStore = key, KeyAlias = "androiddebugkey", StorePasswordEnv = "BEAST_SIGNING_TEST_NOT_SET_6F82" });
    Environment.SetEnvironmentVariable("BEAST_SIGNING_TEST_STORE", "wrong-password");
    await Reject(custom);
    Environment.SetEnvironmentVariable("BEAST_SIGNING_TEST_STORE", "android");
    custom.KeyPasswordEnv = ""; await Sign(unsigned, custom); // Key password defaults to store password.

    BuildConfigManager.CreateDefault(work);
    string configPath = Path.Combine(work, "Config", "BuildConfig.json");
    using (var config = JsonDocument.Parse(File.ReadAllText(configPath)))
        Check(config.RootElement.GetProperty("android").GetProperty("releaseSigning").GetProperty("keyStore").GetString() == "", "Missing generated signing defaults.");
    File.WriteAllText(configPath, "{\"android\":{\"releaseSigning\":{\"keyStore\":42}}}");
    bool badConfig = false;
    try { BuildConfigManager.LoadConfig(work); } catch (InvalidOperationException) { badConfig = true; }
    Check(badConfig, "Malformed signing config must not silently fall back to a debug key.");
    Console.WriteLine("PASS: verified APK, fresh/stable default key, existing signature preservation, custom relative keystore, env passwords, failure safety and generated config.");
    Console.WriteLine($"Test artifacts: {work}");
}
finally
{
    Environment.SetEnvironmentVariable("ANDROID_USER_HOME", savedHome);
    Environment.SetEnvironmentVariable("BEAST_SIGNING_TEST_STORE", null);
    Environment.SetEnvironmentVariable("BEAST_SIGNING_TEST_KEY", null);
}
