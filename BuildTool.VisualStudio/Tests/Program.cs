using Beast.AndroidLauncher;
using System.Text;
using System.Xml.Linq;
using System.IO.Compression;

static void Check(bool value, string message) { if (!value) throw new Exception(message); }
var root = Path.Combine(AppContext.BaseDirectory, "Fixtures", Guid.NewGuid().ToString("N"));
var project = Path.Combine(root, "App O'Brien & $variable; (test)");
Directory.CreateDirectory(Path.Combine(project, "Config"));
File.WriteAllText(Path.Combine(project, "Config", "BuildConfig.json"), "{}");
var exe = Path.Combine(root, "Beast Tool.exe"); File.WriteAllText(exe, "test fixture, never executed");
var file = Path.Combine(project, "App.vcxproj");
XNamespace ns = "http://schemas.microsoft.com/developer/msbuild/2003";
void WriteProject(string tool, string? assembly = null, bool capability = true) {
    new XDocument(new XElement(ns + "Project",
        capability ? new XElement(ns + "ItemGroup", new XElement(ns + "ProjectCapability", new XAttribute("Include", "BeastBuildTool"))) : null,
        new XElement(ns + "PropertyGroup", new XElement(ns + "BeastBuildToolPath", tool),
            new XElement(ns + "BeastProjectPath", "."), assembly == null ? null : new XElement(ns + "BeastBuildToolAssemblyPath", assembly)))).Save(file);
}
WriteProject(exe);
var launch = HotReloadLaunch.Read(file)!;
Check(launch != null && launch.ProjectPath == project && launch.ToolPath == exe, "Resolve selected project metadata");
Check(launch!.Script.Contains("App O''Brien & $variable; (test)' --platform Windows --config Debug"), "Shell metacharacters remain single-quoted data");
Check(launch.StartInfo().WorkingDirectory == project && launch.StartInfo().UseShellExecute, "Native console launch");
var encoded = launch.StartInfo().Arguments.Split(' ').Last();
Check(Encoding.Unicode.GetString(Convert.FromBase64String(encoded)) == launch.Script, "Encoded command round trip");
Check(!launch.IsWatching(), "No watch initially");
var lockDir = Path.Combine(project, "Intermediate", "HotReload"); Directory.CreateDirectory(lockDir);
using (var held = new FileStream(Path.Combine(lockDir, "Windows.lock"), FileMode.Create, FileAccess.ReadWrite, FileShare.None))
    Check(launch.IsWatching(), "Active watcher detected");
Check(!launch.IsWatching(), "Stale lock file does not block launching");
WriteProject(exe, capability: false); Check(HotReloadLaunch.Read(file) == null, "Non-Beast / SDK project hidden");
WriteProject("$(Unknown)/tool.exe"); Check(HotReloadLaunch.Read(file) == null, "Unresolved MSBuild expressions rejected");
WriteProject("missing.exe"); Check(HotReloadLaunch.Read(file) == null, "Missing tool rejected");
WriteProject(Path.Combine("..", "Beast Tool.exe")); Check(HotReloadLaunch.Read(file)?.ToolPath == exe, "Relative executable path");
var dotnet = Path.Combine(root, "dotnet.exe"); File.WriteAllText(dotnet, "test fixture");
WriteProject(dotnet); Check(HotReloadLaunch.Read(file) == null, "Dotnet requires assembly metadata");
var assembly = Path.Combine(root, "Beast.BuildTool.dll"); File.WriteAllText(assembly, "test fixture");
WriteProject(dotnet, assembly); Check(HotReloadLaunch.Read(file)!.Script.Contains("'" + assembly + "' watch "), "Dotnet launch includes assembly");
var repo = Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, "../../../../"));
var vsix = Path.Combine(repo, "Beast.AndroidLauncher", "bin", "Beast.AndroidLauncher.vsix");
using var zip = ZipFile.OpenRead(vsix);
Check(zip.GetEntry("Beast.AndroidLauncher.dll") != null && zip.GetEntry("Beast.AndroidLauncher.pkgdef") != null, "VSIX includes assembly and package registration");
using var manifest = zip.GetEntry("extension.vsixmanifest")!.Open();
var xml = XDocument.Load(manifest); XNamespace vs = "http://schemas.microsoft.com/developer/vsx-schema/2011";
Check(xml.Descendants(vs + "Identity").Single().Attribute("Version")!.Value == "1.2.0", "Upgrade version");
Check(xml.Descendants(vs + "Asset").Count() == 2, "Android MEF plus context menu package");
Console.WriteLine("Launch metadata, quoting, watch lock, dotnet host and VSIX packaging tests passed.");
var configPath = Path.Combine(project, "Config", "BuildConfig.json");
foreach (var config in new[] { "{}", "{\"hotReload\": false}", "{\"hotReload\": true}", "{/*comment*/\"HOTRELOAD\":true,}" }) {
    File.WriteAllText(configPath, config);
    Check(WindowsLaunchPolicy.UseWatch(configPath, "Debug") == config.Contains("true"), "Debug policy reads saved config");
    Check(!WindowsLaunchPolicy.UseWatch(configPath, "Release"), "Release always launches native EXE");
}
foreach (var bad in new[] { "{\"hotReload\":\"true\"}", "{\"hotReload\":null}", "{", "{\"hotReload\":true,\"engine\":{\"enabled\":false}}" }) {
    File.WriteAllText(configPath, bad); bool rejected = false;
    try { WindowsLaunchPolicy.UseWatch(configPath, "Debug"); } catch { rejected = true; }
    Check(rejected, "Invalid launch configuration must not silently fall back");
}
Console.WriteLine("Windows launch policy: true/false/missing, Debug/Release, live changes, comments/trailing commas and invalid config passed.");
