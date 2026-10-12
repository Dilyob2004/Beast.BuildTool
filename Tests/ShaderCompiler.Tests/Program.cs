using Beast.BuildTool;
using System.Reflection;

// Run from the repository root after building ResourcesDemo once.
string demo = Path.GetFullPath("Test/ResourcesDemo");
string project = Path.Combine(demo, "Intermediate", "Verification", "Shader cache tests " + Guid.NewGuid().ToString("N"));
Directory.CreateDirectory(Path.Combine(project, "Source"));
Directory.CreateDirectory(Path.Combine(project, "Config"));
File.Copy(Path.Combine(demo, "Config", "BuildConfig.json"), Path.Combine(project, "Config", "BuildConfig.json"));
BuildConfigManager.LoadConfig(project);
string original = File.ReadAllText(Path.Combine(demo, "Source", "Launch", "Shaders", "UI.hlsl"));
string source = "#include \"Tint.hlsli\"\n#ifndef FACTOR\n#define FACTOR 1.0\n#endif\n" +
    original.Replace("* input.color;", "* input.color * TINT * FACTOR;");
string shaderPath = Path.Combine(project, "Source", "UI.hlsl");
string includePath = Path.Combine(project, "Source", "Tint.hlsli");
File.WriteAllText(shaderPath, source);
File.WriteAllText(includePath, "#define TINT 1.0\n");
var vertex = new ShaderCompileRequest { Name = "Vertex", Source = "Source/UI.hlsl", EntryPoint = "VSMain", Stage = ShaderStage.Vertex };
var fragment = new ShaderCompileRequest { Name = "Fragment", Source = "Source/UI.hlsl", EntryPoint = "PSMain", Stage = ShaderStage.Fragment };
var module = new ModuleRules(); module.Shaders.Add(vertex); module.Shaders.Add(fragment);
var compiler = typeof(ModuleRules).Assembly.GetType("Beast.BuildTool.ShaderCompiler", true)!;
var build = compiler.GetMethod("BuildAsync", BindingFlags.Public | BindingFlags.Static)!;
async Task<string> Compile() => (await (Task<string?>)build.Invoke(null,
    [project, "Windows", TargetConfiguration.Debug, new[] { module }])!)!;
void Check(bool okay, string message) { if (!okay) throw new Exception(message); }
async Task Reject(Func<Task> action, string message)
{
    bool rejected = false;
    try { await action(); } catch (Exception error) when (error is InvalidOperationException or FileNotFoundException)
    { rejected = true; Console.WriteLine("Expected failure: " + error.Message.Split('\n')[0]); }
    Check(rejected, message);
}
string header = await Compile();
string first = File.ReadAllText(header); DateTime timestamp = File.GetLastWriteTimeUtc(header);
await Compile(); Check(File.GetLastWriteTimeUtc(header) == timestamp, "Cache hit rewrote the header.");
File.WriteAllText(includePath, "#define TINT 0.75\n");
await Compile(); Check(File.ReadAllText(header) != first, "Transitive include change was ignored.");
string included = File.ReadAllText(header);
fragment.Defines["FACTOR"] = "0.5";
await Compile(); Check(File.ReadAllText(header) != included, "Macro change was ignored.");
string beforeFailure = File.ReadAllText(header);
File.WriteAllText(shaderPath, source + "\nthis is invalid HLSL;\n");
await Reject(async () => { await Compile(); }, "Bad HLSL compiled.");
Check(File.ReadAllText(header) == beforeFailure, "A failed build replaced the last good package.");
File.WriteAllText(shaderPath, source.Replace("[[vk::binding(0, 0)]]", "[[vk::binding(5, 0)]]"));
await Reject(async () => { await Compile(); }, "Unsupported resource binding was accepted.");
File.WriteAllText(shaderPath, source);
await Compile();
string binary = Path.Combine(Path.GetDirectoryName(header)!, "Fragment.spv");
File.WriteAllBytes(binary, [0, 0, 0, 0]);
await Compile(); Check(File.ReadAllBytes(binary).Length > 20, "Corrupt cache was reused.");
File.Delete(header); await Compile(); Check(File.Exists(header), "Cache did not restore a missing generated header.");
module.Shaders.Add(new ShaderCompileRequest { Name = "Vertex", Source = "Source/UI.hlsl" });
await Reject(async () => { await Compile(); }, "Duplicate shader name was accepted.");
module.Shaders.RemoveAt(module.Shaders.Count - 1);
vertex.Name = "class";
await Reject(async () => { await Compile(); }, "C++ keyword was accepted as a generated function name.");
vertex.Name = "Vertex";
// ABI v2 is an optional, bounded fragment constant block; v1 remains valid.
const string constants = "struct DrawConstants { float4 values[8]; };\n[[vk::push_constant]] ConstantBuffer<DrawConstants> BeastDraw;\n";
string withConstants = constants + source.Replace("* input.color * TINT * FACTOR;", "* input.color * TINT * FACTOR * BeastDraw.values[7].x;");
File.WriteAllText(shaderPath, withConstants);
await Compile(); Check(File.ReadAllText(header).Contains("d.constantsUniform = \"BeastDraw.values[0]\""), "Missing portable draw-constant metadata.");
File.WriteAllText(shaderPath, withConstants.Replace("values[8]", "values[16]"));
await Reject(async () => { await Compile(); }, "Oversized push-constant block was accepted.");
File.WriteAllText(shaderPath, withConstants.Replace("BeastDraw", "WrongBlock"));
await Reject(async () => { await Compile(); }, "Nonportable constant block name was accepted.");
File.WriteAllText(shaderPath, source); await Compile();
string dxc = BuildConfigManager.Config.ShaderTools.Dxc;
BuildConfigManager.Config.ShaderTools.Dxc = "missing-dxc.exe";
await Reject(async () => { await Compile(); }, "Missing explicit tool silently fell back.");
BuildConfigManager.Config.ShaderTools.Dxc = dxc;
Console.WriteLine("Shader compiler tests passed: cache, includes, defines, paths with spaces, corruption, errors, ABI, names, tool configuration.");
Console.WriteLine("Artifacts: " + project);
