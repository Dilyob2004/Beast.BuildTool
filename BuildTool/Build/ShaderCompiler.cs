using System.Diagnostics;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;

namespace Beast.BuildTool;

public enum ShaderStage { Vertex, Fragment }
public sealed class ShaderCompileRequest
{
    public string Name { get; set; } = "";
    public string Source { get; set; } = "";
    public string EntryPoint { get; set; } = "main";
    public ShaderStage Stage { get; set; }
    public Dictionary<string, string> Defines { get; } = new();
}

/// <summary>Offline HLSL -> validated SPIR-V + desktop/mobile GLSL. No compiler ships in the APK.</summary>
internal static class ShaderCompiler
{
    private const string CacheVersion = "BeastShaderABI-2";
    private static readonly Regex Identifier = new("^[A-Za-z_][A-Za-z0-9_]*$", RegexOptions.CultureInvariant);
    private static readonly HashSet<string> CppKeywords = new((
        "alignas alignof and and_eq asm atomic_cancel atomic_commit atomic_noexcept auto bitand bitor bool break case catch char char8_t char16_t char32_t class compl concept const consteval constexpr constinit const_cast continue co_await co_return co_yield decltype default delete do double dynamic_cast else enum explicit export extern false float for friend goto if inline int long mutable namespace new noexcept not not_eq nullptr operator or or_eq private protected public register reinterpret_cast requires return short signed sizeof static static_assert static_cast struct switch synchronized template this thread_local throw true try typedef typeid typename union unsigned using virtual void volatile wchar_t while xor xor_eq").Split(' '), StringComparer.Ordinal);
    private static string Hash(byte[] bytes) => Convert.ToHexString(SHA256.HashData(bytes));
    private static string Hash(string text) => Hash(Encoding.UTF8.GetBytes(text));

    public static async Task<string?> BuildAsync(string project, string platform, TargetConfiguration configuration,
        IEnumerable<ModuleRules> modules)
    {
        var shaders = modules.SelectMany(m => m.Shaders).OrderBy(s => s.Name, StringComparer.Ordinal).ToArray();
        if (shaders.Length == 0) return null;
        if (!BuildConfigManager.Config.Engine.Enabled) throw new InvalidOperationException("Portable shaders require engine.enabled=true.");
        var names = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (var shader in shaders)
        {
            if (!Identifier.IsMatch(shader.Name) || shader.Name.StartsWith('_') || CppKeywords.Contains(shader.Name) ||
                !names.Add(shader.Name) || !Identifier.IsMatch(shader.EntryPoint))
                throw new InvalidOperationException("Shader names must be unique C++ identifiers; entry points must be identifiers.");
            if (!Enum.IsDefined(shader.Stage)) throw new InvalidOperationException("Unsupported shader stage.");
            if (!File.Exists(Path.GetFullPath(shader.Source, project))) throw new FileNotFoundException("Shader source not found.", shader.Source);
            foreach (var define in shader.Defines)
                if (!Identifier.IsMatch(define.Key) || define.Value.Contains('\n') || define.Value.Contains('\r'))
                    throw new InvalidOperationException("Invalid shader definition: " + define.Key);
        }
        var config = BuildConfigManager.Config.ShaderTools;
        string dxc = ResolveTool(project, config.Dxc, "dxc");
        string cross = ResolveTool(project, config.SpirvCross, "spirv-cross");
        string validator = ResolveTool(project, config.SpirvValidator, "spirv-val");
        var dependencies = new[] { dxc, cross, validator,
            Path.Combine(Path.GetDirectoryName(dxc)!, "dxcompiler.dll"),
            Path.Combine(Path.GetDirectoryName(cross)!, "spirv-cross-c-shared.dll") };
        string toolsHash = string.Join(";", dependencies.Where(File.Exists).Select(p => Hash(File.ReadAllBytes(p))));
        string directory = Path.Combine(project, "Intermediate", "Build", platform, configuration.ToString(), "Shaders");
        Directory.CreateDirectory(directory);
        // Serialize shader generation for this platform/configuration, including header publication.
        using var buildLock = new FileStream(Path.Combine(directory, "build.lock"), FileMode.OpenOrCreate, FileAccess.ReadWrite, FileShare.None);
        var header = new StringBuilder("#pragma once\n#include \"RHI/RHI.h\"\nnamespace BeastShaders\n{\n");
        foreach (var shader in shaders)
        {
            string source = Path.GetFullPath(shader.Source, project);
            string prefix = Path.Combine(directory, shader.Name);
            var arguments = new List<string> { "-spirv", "-fspv-target-env=vulkan1.0", "-fvk-use-gl-layout", "-Ges", "-WX",
                "-T", shader.Stage == ShaderStage.Vertex ? "vs_6_0" : "ps_6_0", "-E", shader.EntryPoint,
                "-I", Path.GetDirectoryName(source)!, "-I", Path.Combine(project, "Source") };
            // Strip resources belonging only to other entry points in a shared HLSL file.
            // Native Debug/Release is independent of shader optimization for ABI v1.
            arguments.Add("-O3");
            foreach (var define in shader.Defines.OrderBy(d => d.Key, StringComparer.Ordinal))
            { arguments.Add("-D"); arguments.Add(define.Key + "=" + define.Value); }
            arguments.Add(source);
            // DXC preprocessing tracks transitive includes and conditional macros, unlike timestamp-only caches.
            await Run(dxc, [.. arguments, "-P", "-Fi", prefix + ".preprocessed.hlsl"], project);
            string key = Hash(CacheVersion + toolsHash + string.Join('\n', arguments) + File.ReadAllText(prefix + ".preprocessed.hlsl"));
            string[] outputs = [prefix + ".spv", prefix + ".glsl", prefix + ".gles", prefix + ".reflection.json"];
            string cache = prefix + ".cache";
            string Signature() => key + "\n" + string.Join('\n', outputs.Select(p => Hash(File.ReadAllBytes(p))));
            bool hit = outputs.All(File.Exists) && File.Exists(cache) && File.ReadAllText(cache) == Signature();
            if (!hit)
            {
                Log.TraceInfo($"Compiling shader {shader.Name} ({shader.Stage}): HLSL -> SPIR-V / GLSL 460 / GLSL ES 300");
                await Run(dxc, [.. arguments, "-Fo", outputs[0]], project);
                await Run(validator, ["--target-env", "vulkan1.0", outputs[0]], project);
                string reflection = await Run(cross, [outputs[0], "--reflect"], project);
                File.WriteAllText(outputs[3], reflection);
                ValidateReflection(shader, reflection);
                var translate = new List<string> { outputs[0], "--combined-samplers-inherit-bindings" };
                string direction = shader.Stage == ShaderStage.Vertex ? "out" : "in";
                for (int location = 0; location < 2; ++location)
                    translate.AddRange(["--rename-interface-variable", direction, location.ToString(), "BeastLocation" + location]);
                if (shader.Stage == ShaderStage.Vertex) translate.AddRange(["--flip-vert-y", "--fixup-clipspace"]);
                await Run(cross, [.. translate, "--version", "460", "--output", outputs[1]], project);
                await Run(cross, [.. translate, "--es", "--version", "300", "--output", outputs[2]], project);
                AtomicWrite(cache, Signature());
            }
            else Log.TraceInfo("Shader cache hit: " + shader.Name);
            ValidateReflection(shader, File.ReadAllText(outputs[3]));
            byte[] binary = File.ReadAllBytes(outputs[0]);
            if (binary.Length < 20 || binary.Length % 4 != 0) throw new InvalidOperationException("Invalid shader binary: " + shader.Name);
            string glsl = File.ReadAllText(outputs[1]), gles = File.ReadAllText(outputs[2]);
            string sampler = "";
            if (shader.Stage == ShaderStage.Fragment)
            {
                const string pattern = @"uniform\s+(?:(?:highp|mediump|lowp)\s+)?sampler2D\s+(\w+)\s*;";
                var desktop = Regex.Matches(glsl, pattern); var mobile = Regex.Matches(gles, pattern);
                if (desktop.Count != 1 || mobile.Count != 1 || desktop[0].Groups[1].Value != mobile[0].Groups[1].Value)
                    throw new InvalidOperationException("Expected exactly one matching combined sampler in generated GLSL: " + shader.Name);
                sampler = desktop[0].Groups[1].Value;
            }
            if (glsl.Contains(")BEAST_GLSL\"") || gles.Contains(")BEAST_GLSL\"")) throw new InvalidOperationException("Reserved GLSL delimiter.");
            header.AppendLine($"inline Beast::RHIShaderHandle {shader.Name}()\n{{\n    Beast::RHIShaderDesc d;");
            header.AppendLine($"    d.name = \"{shader.Name}\"; d.stage = Beast::RHIShaderStage::{shader.Stage}; d.entryPoint = \"{shader.EntryPoint}\";");
            header.Append("    d.spirv = {");
            for (int i = 0; i < binary.Length; i += 4)
            {
                if (i % 32 == 0) header.Append("\n        ");
                header.Append("0x").Append(System.Buffers.Binary.BinaryPrimitives.ReadUInt32LittleEndian(binary.AsSpan(i, 4)).ToString("x8")).Append("u,");
            }
            header.AppendLine("\n    };");
            header.AppendLine($"    d.glsl460 = R\"BEAST_GLSL({glsl})BEAST_GLSL\";");
            header.AppendLine($"    d.glslES300 = R\"BEAST_GLSL({gles})BEAST_GLSL\";");
            header.AppendLine($"    d.combinedSampler = \"{sampler}\";");
            // The optional v2 constant block has a deliberately fixed portable ABI.
            using (var reflection = JsonDocument.Parse(File.ReadAllText(outputs[3])))
                if (reflection.RootElement.TryGetProperty("push_constants", out var constants) && constants.GetArrayLength() != 0)
                {
                    const string uniform = "BeastDraw.values[0]";
                    if (!Regex.IsMatch(glsl, @"uniform\s+\w+\s+BeastDraw\s*;") || !Regex.IsMatch(gles, @"uniform\s+\w+\s+BeastDraw\s*;"))
                        throw new InvalidOperationException("Unexpected generated push-constant uniform: " + shader.Name);
                    header.AppendLine($"    d.constantsUniform = \"{uniform}\";");
                }
            header.AppendLine("    return Beast::RHI::CreateShader(std::move(d));\n}");
        }
        header.AppendLine("}");
        string output = Path.Combine(directory, "BeastShaders.generated.h");
        AtomicWrite(output, header.ToString());
        return output;
    }

    private static void ValidateReflection(ShaderCompileRequest shader, string json)
    {
        using var document = JsonDocument.Parse(json);
        var root = document.RootElement;
        void Fail(string reason) => throw new InvalidOperationException($"Shader {shader.Name}: {reason} (Beast shader ABI v2).");
        foreach (var property in root.EnumerateObject())
            if (property.Name is not ("entryPoints" or "types" or "inputs" or "outputs" or "separate_images" or "separate_samplers" or "push_constants"))
                Fail("Unsupported reflection/resource field: " + property.Name);
        if (root.TryGetProperty("push_constants", out var constants) && constants.GetArrayLength() != 0)
        {
            if (shader.Stage != ShaderStage.Fragment || constants.GetArrayLength() != 1) Fail("Expected one fragment constant block");
            var block = constants[0];
            if (block.GetProperty("name").GetString() != "BeastDraw") Fail("Constant block must be named BeastDraw");
            var members = root.GetProperty("types").GetProperty(block.GetProperty("type").GetString()!).GetProperty("members");
            if (members.GetArrayLength() != 1) Fail("Constant block must contain only float4 values[8]");
            var member = members[0];
            if (member.GetProperty("name").GetString() != "values" || member.GetProperty("type").GetString() != "vec4" ||
                member.GetProperty("offset").GetInt32() != 0 || member.GetProperty("array_stride").GetInt32() != 16 ||
                member.GetProperty("array").GetArrayLength() != 1 || member.GetProperty("array")[0].GetInt32() != 8 ||
                !member.GetProperty("array_size_is_literal")[0].GetBoolean()) Fail("Expected tightly packed float4 values[8]");
        }
        var entries = root.GetProperty("entryPoints");
        if (entries.GetArrayLength() != 1 || entries[0].GetProperty("name").GetString() != shader.EntryPoint ||
            entries[0].GetProperty("mode").GetString() != (shader.Stage == ShaderStage.Vertex ? "vert" : "frag")) Fail("Entry point mismatch");
        void Interface(string field, params string[] types)
        {
            if (!root.TryGetProperty(field, out var items) || items.GetArrayLength() != types.Length) { Fail("Unexpected " + field); return; }
            var seen = new HashSet<int>();
            foreach (var item in items.EnumerateArray())
            {
                int location = item.GetProperty("location").GetInt32();
                if (location < 0 || location >= types.Length || !seen.Add(location) || item.GetProperty("type").GetString() != types[location] || item.TryGetProperty("array", out _))
                    Fail("Invalid " + field + " location/type");
            }
        }
        Interface("inputs", shader.Stage == ShaderStage.Vertex ? ["vec2", "vec2", "vec4"] : ["vec2", "vec4"]);
        Interface("outputs", shader.Stage == ShaderStage.Vertex ? ["vec2", "vec4"] : ["vec4"]);
        void Resource(string field, string type, int binding)
        {
            bool exists = root.TryGetProperty(field, out var items) && items.GetArrayLength() > 0;
            if (shader.Stage == ShaderStage.Vertex) { if (exists) Fail("Vertex resources are not supported yet"); return; }
            if (!exists || items.GetArrayLength() != 1) { Fail("Expected one " + field); return; }
            var item = items[0];
            if (item.GetProperty("type").GetString() != type || item.GetProperty("set").GetInt32() != 0 ||
                item.GetProperty("binding").GetInt32() != binding || item.TryGetProperty("array", out _)) Fail("Unexpected " + field + " binding/type");
        }
        Resource("separate_images", "texture2D", 0);
        Resource("separate_samplers", "sampler", 1);
    }

    private static string ResolveTool(string project, string configured, string name)
    {
        string executable = name + (OperatingSystem.IsWindows() ? ".exe" : "");
        if (!string.IsNullOrWhiteSpace(configured))
        {
            string path = Path.GetFullPath(configured, project);
            if (!File.Exists(path)) throw new FileNotFoundException("Configured shader tool not found.", path);
            return path;
        }
        var directories = new List<string>();
        string? sdk = Environment.GetEnvironmentVariable("VULKAN_SDK");
        if (!string.IsNullOrWhiteSpace(sdk)) { directories.Add(Path.Combine(sdk, "Bin")); directories.Add(Path.Combine(sdk, "bin")); }
        directories.AddRange((Environment.GetEnvironmentVariable("PATH") ?? "").Split(Path.PathSeparator).Where(p => !string.IsNullOrWhiteSpace(p)));
        foreach (string directory in directories)
        {
            string path = Path.Combine(directory.Trim('"'), executable);
            if (File.Exists(path)) return Path.GetFullPath(path);
        }
        throw new FileNotFoundException($"Shader tool '{executable}' not found. Set shaderTools in Config/BuildConfig.json or VULKAN_SDK/PATH.");
    }
    private static async Task<string> Run(string executable, IEnumerable<string> arguments, string directory)
    {
        var info = new ProcessStartInfo(executable) { WorkingDirectory = directory, UseShellExecute = false,
            RedirectStandardOutput = true, RedirectStandardError = true, CreateNoWindow = true };
        foreach (string argument in arguments) info.ArgumentList.Add(argument);
        using var process = Process.Start(info) ?? throw new InvalidOperationException("Could not start " + executable);
        Task<string> stdout = process.StandardOutput.ReadToEndAsync(), stderr = process.StandardError.ReadToEndAsync();
        await process.WaitForExitAsync();
        string output = await stdout, errors = await stderr;
        if (process.ExitCode != 0) throw new InvalidOperationException($"{Path.GetFileName(executable)} failed ({process.ExitCode}):\n{output}\n{errors}");
        if (!string.IsNullOrWhiteSpace(errors)) Log.TraceInfo(errors.Trim());
        return output;
    }
    private static void AtomicWrite(string path, string content)
    {
        if (File.Exists(path) && File.ReadAllText(path) == content) return;
        string temporary = path + ".tmp";
        File.WriteAllText(temporary, content, new UTF8Encoding(false));
        File.Move(temporary, path, true);
    }
}
