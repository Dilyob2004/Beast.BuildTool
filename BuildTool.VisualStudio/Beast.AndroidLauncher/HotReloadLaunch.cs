using System;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text;
using System.Xml;
using System.Xml.Linq;

namespace Beast.AndroidLauncher;

// No shell interpretation of project paths: the generated PowerShell program
// consists of quoted literals and fixed arguments, transported as UTF-16 base64.
internal sealed class HotReloadLaunch
{
    public string ToolPath { get; }
    public string ProjectPath { get; }
    public string? ToolAssembly { get; }
    private HotReloadLaunch(string tool, string project, string? assembly)
    { ToolPath = tool; ProjectPath = project; ToolAssembly = assembly; }

    public static HotReloadLaunch? Read(string projectFile)
    {
        if (!projectFile.EndsWith(".vcxproj", StringComparison.OrdinalIgnoreCase) || !File.Exists(projectFile)) return null;
        var settings = new XmlReaderSettings { DtdProcessing = DtdProcessing.Prohibit, XmlResolver = null };
        using var reader = XmlReader.Create(projectFile, settings);
        var xml = XDocument.Load(reader); XNamespace ns = "http://schemas.microsoft.com/developer/msbuild/2003";
        if (!xml.Descendants(ns + "ProjectCapability").Any(e => (string?)e.Attribute("Include") == "BeastBuildTool")) return null;
        string? Resolve(string name)
        {
            string? value = xml.Descendants(ns + name).FirstOrDefault()?.Value.Trim();
            if (string.IsNullOrEmpty(value) || value!.Contains("$(") || value.IndexOfAny(new[] { '\r', '\n', '\0' }) >= 0) return null;
            return Path.GetFullPath(Path.IsPathRooted(value) ? value : Path.Combine(Path.GetDirectoryName(projectFile)!, value));
        }
        string? tool = Resolve("BeastBuildToolPath"), project = Resolve("BeastProjectPath");
        if (tool == null || project == null || !File.Exists(tool) || !File.Exists(Path.Combine(project, "Config", "BuildConfig.json"))) return null;
        string? assembly = null;
        if (Path.GetFileName(tool).Equals("dotnet.exe", StringComparison.OrdinalIgnoreCase))
        {
            assembly = Resolve("BeastBuildToolAssemblyPath");
            if (assembly == null || !File.Exists(assembly)) return null;
        }
        else if (!Path.GetExtension(tool).Equals(".exe", StringComparison.OrdinalIgnoreCase)) return null;
        return new HotReloadLaunch(tool, project, assembly);
    }
    public bool IsWatching()
    {
        string path = Path.Combine(ProjectPath, "Intermediate", "HotReload", "Windows.lock");
        if (!File.Exists(path)) return false;
        try { using var stream = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite); return false; }
        catch (IOException) { return true; }
    }
    private static string Literal(string value) => "'" + value.Replace("'", "''") + "'";
    public string Script => "$Host.UI.RawUI.WindowTitle = 'Beast Hot Reload - Windows (Debug)'; " +
        "Set-Location -LiteralPath " + Literal(ProjectPath) + "; & " + Literal(ToolPath) +
        (ToolAssembly == null ? "" : " " + Literal(ToolAssembly)) + " watch " + Literal(ProjectPath) +
        " --platform Windows --config Debug; Write-Host ('Beast watch finished. Exit code: ' + $LASTEXITCODE)";
    public ProcessStartInfo StartInfo() => new ProcessStartInfo
    {
        FileName = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.System), "WindowsPowerShell", "v1.0", "powershell.exe"),
        Arguments = "-NoLogo -NoProfile -NoExit -EncodedCommand " + Convert.ToBase64String(Encoding.Unicode.GetBytes(Script)),
        WorkingDirectory = ProjectPath,
        UseShellExecute = true,
        WindowStyle = ProcessWindowStyle.Normal
    };
}
