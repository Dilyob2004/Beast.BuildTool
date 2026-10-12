using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace Beast.BuildTool
{


    public enum ModuleType
    {
        Source,
        External,
        HeaderOnly
    }
    public class ModulesInfo
    {
        public readonly TargetConfiguration Configuration;
        public readonly PlatformType Platform;
        public readonly PlatformArch Architecture;
        public string ConfigurationName
        {
            get
            {
                switch (Configuration)
                {
                    case TargetConfiguration.Debug: return "Debug";
                    case TargetConfiguration.Development: return "Development";
                    case TargetConfiguration.Release: return "Release";
                    default: return "Development";
                }
            }
        }
        public string ThirdPartyDirectory
        {
            get
            {
                return GlobalEnvironment.ThirdPartyPath;
            }
        }
        public string SourceDirectory
        {
            get
            {
                return GlobalEnvironment.SourcePath;
            }
        }
        public string? ProjectDirectory => GlobalEnvironment.ProjectPath;
        public string? ProjectSourceDirectory => GlobalEnvironment.ProjectPath != null ? System.IO.Path.Combine(GlobalEnvironment.ProjectPath, "Source") : null;
        public ModulesInfo(TargetConfiguration configuration, Platform platform)
        {
            Configuration = configuration;
            Platform = platform.Type;
            Architecture = platform.Architecture;
        }
    }
    public class ModuleRules
    {

        public ModuleType Type;

        public HashSet<string> PublicIncludePaths { get; } = new HashSet<string>();
        public HashSet<string> PrivateIncludePaths { get; } = new HashSet<string>();

        public HashSet<string> Definitions { get; } = new HashSet<string>();
        /// <summary>Portable HLSL shaders; Source paths are relative to the project root.</summary>
        public List<ShaderCompileRequest> Shaders { get; } = new();

        public HashSet<string> PublicSystemIncludePaths { get; } = new HashSet<string>();
        public HashSet<string> PrivateSystemIncludePaths { get; } = new HashSet<string>();

        public HashSet<string> InputLibraries { get; } = new HashSet<string>();
        public HashSet<string> DelayLoadLibraries { get; } = new HashSet<string>();
        public HashSet<string> RuntimeDependencies { get; } = new HashSet<string>();
        public HashSet<string> DirectoryDependencies { get; } = new HashSet<string>();
        public HashSet<string> ThirdPartyDependices { get; } = new HashSet<string>();

        public string PrecompiledHeaderSource = "";

        public string ModuleName = "";
        public string ModulePath = "";

    }
    public class BuildInfo
    {

        public HashSet<string> IncludePaths { get; set; } = new HashSet<string>();
        public HashSet<string> Definitions { get; set; } = new HashSet<string>();
        public HashSet<string> SourceFiles { get; set; } = new HashSet<string>();
    }
}
