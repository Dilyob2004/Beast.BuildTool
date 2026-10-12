using System;
using System.Collections.Generic;

namespace Beast.BuildTool
{
    public enum PlatformArch
    {
        X64,
        ARM64
    }

    public class InvalidArchitectureException : Exception
    {
        public InvalidArchitectureException(PlatformArch architecture)
        : base(string.Format("Unknown architecture {0}.", architecture))
        {
        }
        public InvalidArchitectureException(PlatformArch architecture, string message)
        : base(string.Format("Unknown architecture {0}. " + message, architecture))
        {
        }
    }
    public enum PlatformType
    {
        Android,
        Windows
    }
    public abstract class Platform
    {

        public abstract PlatformArch Architecture { get; }
        public abstract PlatformType Type { get; }

        public readonly HashSet<string> SystemIncludePaths = new HashSet<string>();
        public readonly HashSet<string> SystemLibraryPaths = new HashSet<string>();

        public abstract string SharedLibraryFileExtension { get; }
        public abstract string PlatformName { get; }

        /// <inheritdoc />
        public abstract string StaticLibraryFileExtension { get; }
        public abstract bool IsValid();
        public virtual void SetupCPPEnvironment(CPPEnvironment _Environment)
        {
        }
        public virtual void SetupLinkEnvironment(LinkEnvironment LinkEnv)
        {
        }
        public abstract CPPOutput CompileCppFiles(CommandExecutor Executor, CPPEnvironment Environment, List<string> sourceFiles, string outputPath);

        public abstract void LinkFiles(CommandExecutor Executor, LinkEnvironment Env, string outputFilePath);
        public abstract void ArchiveFiles(CommandExecutor executor, List<string> objects, string outputPath);
    }
}
