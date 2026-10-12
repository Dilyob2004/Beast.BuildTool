using Microsoft.VisualBasic;
using Microsoft.Win32;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading.Tasks;
namespace Beast.BuildTool
{

    public enum WindowsCompiler
    {
        Clang,
        VisualStudio,
    }

    public class WindowsPlatform : Platform
    {
        /// <summary>
        /// The VC tools root path.
        /// </summary>
        protected readonly string ?VCToolPath;

        /// <summary>
        /// The compiler path.
        /// </summary>
        protected readonly string ?CompilerPath;

        /// <summary>
        /// The resource compiler path.
        /// </summary>
        public readonly string? ResourceCompilerPath;

        /// <summary>
        /// The linker path.
        /// </summary>
        protected readonly string ?LinkerPath;

        /// <summary>
        /// The library tool path.
        /// </summary>
        protected readonly string ? LibToolPath;


        public override string SharedLibraryFileExtension => ".dll";
        public string DumpBinPath => Path.Combine(Path.GetDirectoryName(CompilerPath!)!, "dumpbin.exe");

        /// <inheritdoc />
        public override string StaticLibraryFileExtension => ".lib";
        public override string PlatformName => "Windows";


        public override PlatformType Type { get; } = PlatformType.Windows;
        public override PlatformArch Architecture { get; } = PlatformArch.X64;

        private Version? MinVersion = new Version(0, 0);

        public override void SetupLinkEnvironment(LinkEnvironment LinkEnv)
        {
            base.SetupLinkEnvironment(LinkEnv);
            LinkEnv.InputLibraries.Add("dwmapi.lib");
            LinkEnv.InputLibraries.Add("kernel32.lib");
            LinkEnv.InputLibraries.Add("user32.lib");
            LinkEnv.InputLibraries.Add("comdlg32.lib");
            LinkEnv.InputLibraries.Add("advapi32.lib");
            LinkEnv.InputLibraries.Add("shell32.lib");
            LinkEnv.InputLibraries.Add("ole32.lib");
            LinkEnv.InputLibraries.Add("oleaut32.lib");
            LinkEnv.InputLibraries.Add("delayimp.lib");
            LinkEnv.InputLibraries.Add("gdi32.lib");
            LinkEnv.InputLibraries.Add("Ws2_32.lib");


            LinkEnv.ExcludedLibraries.Add("LIBC");
            LinkEnv.ExcludedLibraries.Add("LIBCP");
            LinkEnv.ExcludedLibraries.Add("LIBCD");
            LinkEnv.ExcludedLibraries.Add("LIBCPD");
        }
        public override void SetupCPPEnvironment(CPPEnvironment Environment)
        {
            base.SetupCPPEnvironment(Environment);
            Environment.PreprocessorDefinitions.Add("_CRT_SECURE_NO_WARNINGS");
            //Environment.PreprocessorDefinitions.Add("_CRT_SECURE_NO_DEPRECATE");
            //Environment.PreprocessorDefinitions.Add("_CRT_USE_BUILTIN_OFFSETOF");


            /*if (Environment.CppVersion == CppVersion.Latest)
                Environment.PreprocessorDefinitions.Add("_SILENCE_CXX23_ALIGNED_STORAGE_DEPRECATION_WARNING");
            if (Environment.CppVersion == CppVersion.Cpp20)
                Environment.PreprocessorDefinitions.Add("_SILENCE_CXX20_ALIGNED_STORAGE_DEPRECATION_WARNING");
            else if(Environment.CppVersion == CppVersion.Cpp17)
                Environment.PreprocessorDefinitions.Add("_SILENCE_CXX17_ALIGNED_STORAGE_DEPRECATION_WARNING");
            */
            Environment.PreprocessorDefinitions.Add("PLATFORM_WINDOWS");
            Environment.PreprocessorDefinitions.Add("PLATFORM_WIN32");
            Environment.PreprocessorDefinitions.Add("WIN32");
            Environment.PreprocessorDefinitions.Add("WIN64");
            Environment.PreprocessorDefinitions.Add("_WINDOWS");
        }

        public override bool IsValid()
        {

            if(string.IsNullOrEmpty(VCToolPath) || !File.Exists(CompilerPath) || !File.Exists(LinkerPath) || !File.Exists(LibToolPath) || !File.Exists(ResourceCompilerPath))
            {
                return false;
            }

            return true;
        }
        public WindowsPlatform()
        {
            if (VisualStudioInstance.VCToolChainPath == null)
            {
                VisualStudioInstance.EnsureVCToolPath();
            }

            VCToolPath = VisualStudioInstance.VCToolChainPath;

            if (VCToolPath == null)
                throw new Exception("Visual Studio VC Tool Path not found");
            string BinaryPath = Path.Combine(VCToolPath, "bin", "Hostx64", "X64");

            CompilerPath = Path.Combine(BinaryPath, "cl.exe");
            LinkerPath = Path.Combine(BinaryPath, "link.exe");
            LibToolPath = Path.Combine(BinaryPath, "lib.exe");




            SystemIncludePaths.Add(Path.Combine(VCToolPath, "include"));
            SystemLibraryPaths.Add(Path.Combine(VCToolPath, "lib", "x64"));

            if (TryGetLatestWindowsSdkDir(out string? SDKDir, out List<Version>? Versions))
            {

                if (string.IsNullOrEmpty(SDKDir) || Versions == null || Versions.Count == 0)
                {
                    throw new Exception("Windows SDK not found");
                }

                MinVersion = Versions.Last();
                string LatestSDKVersionName = MinVersion.ToString();

                string includeRootDir = Path.Combine(SDKDir, "include", LatestSDKVersionName);
                {
                    SystemIncludePaths.Add(Path.Combine(includeRootDir, "ucrt"));
                    SystemIncludePaths.Add(Path.Combine(includeRootDir, "shared"));
                    SystemIncludePaths.Add(Path.Combine(includeRootDir, "um"));
                    SystemIncludePaths.Add(Path.Combine(includeRootDir, "winrt"));
                }
                string libraryRootDir = Path.Combine(SDKDir, "lib", LatestSDKVersionName);
                {
                    SystemLibraryPaths.Add(Path.Combine(libraryRootDir, "ucrt", "x64"));
                    SystemLibraryPaths.Add(Path.Combine(libraryRootDir, "um", "x64"));
                }
                var binRootDir = Path.Combine(SDKDir, "bin", LatestSDKVersionName, "x64");
                {
                    ResourceCompilerPath = Path.Combine(binRootDir, "rc.exe");
                }
            }
            else
            {
                throw new Exception("Windows SDK not found");
            }
        }
        private void AppendArgs_CPP(CPPEnvironment CompileEnvironment, List<string> Arguments)
        {
            Arguments.Add("/TP");
            // C++ version
            switch (CompileEnvironment.CppVersion)
            {
                case CppVersion.Cpp14:
                    Arguments.Add("/std:c++14");
                    break;
                case CppVersion.Cpp17:
                    Arguments.Add("/std:c++17");
                    break;
                case CppVersion.Cpp20:
                    Arguments.Add("/std:c++20");
                    break;
                case CppVersion.Latest:
                    Arguments.Add("/std:c++latest");
                    break;
            }
            Arguments.Add("/Zc:__cplusplus");


            // Enable Run-Time Type Information
            if (CompileEnvironment.RuntimeTypeInfo)
                Arguments.Add("/GR");
            else
                Arguments.Add("/GR-");
            // Buffer Security Check
            if (CompileEnvironment.BufferSecurityCheck)
                Arguments.Add("/GS");
            else
                Arguments.Add("/GS-");
        }
        private void AppendArgs_C(CPPEnvironment CompileEnvironment, List<string> Arguments)
        {
            // Explicitly compile the file as C.
            Arguments.Add("/TC");

            // Level 0 warnings.  Needed for external C projects that produce warnings at higher warning levels.
            Arguments.Add("/W0");

            Arguments.Add("/Zc:strictStrings-"); 
            Arguments.Add("/Zc:ternary-");
        }


        public override CPPOutput CompileCppFiles(CommandExecutor Executor, CPPEnvironment Environment, List<string> sourceFiles, string outputPath)
        {
            var Output = new CPPOutput();
            
            // Initialize dependency cache for this output path
            var depCache = new DependencyCache(outputPath);

            var CommonArgs = new List<string>();
            {
                // Windows 11 requires SSE4.2

                if (MinVersion?.Major >= 11)
                    CommonArgs.Add("/d2archSSE42");

                // Suppress Startup Banner
                CommonArgs.Add("/nologo");
                // Public UI strings and sources use UTF-8 on every platform.
                CommonArgs.Add("/utf-8");

                // Compile Without Linking
                CommonArgs.Add("/c");

                // Generate Intrinsic Functions
                if (Environment.IntrinsicFunctions)
                    CommonArgs.Add("/Oi");

                // Enable Function-Level Linking
                if (Environment.FunctionLevelLinking)
                    CommonArgs.Add("/Gy");
                else
                    CommonArgs.Add("/Gy-");


                // List Include Files
                //CommonArgs.Add("/showIncludes");

                // Code Analysis
                CommonArgs.Add("/analyze-");

                // Remove unreferenced COMDAT
                CommonArgs.Add("/Zc:inline");

                // Run-Time Error Checks
                if (Environment.RuntimeChecks)
                    CommonArgs.Add("/RTC1");

                // Inline Function Expansion
                if (Environment.Inlining)
                    CommonArgs.Add("/Ob2");

                if (Environment.DebugInformation)
                {
                    // Debug Information Format
                    //if (UseSeparatePdb)
                    CommonArgs.Add("/Zi");
                    //else
                    //CommonArgs.Add("/Z7");

                    // Enhance Optimized Debugging
                    CommonArgs.Add("/Zo");
                }

                // Favor Small Code, Favor Fast Code
                if (Environment.FavorSizeOrSpeed == FavorSizeOrSpeed.FastCode)
                    CommonArgs.Add("/Ot");
                else if (Environment.FavorSizeOrSpeed == FavorSizeOrSpeed.SmallCode)
                    CommonArgs.Add("/Os");
                if (Environment.Optimization)
                {
                    // Generate Intrinsic Functions
                    CommonArgs.Add("/Oi");

                    // Frame-Pointer Omission
                    CommonArgs.Add("/Oy");

                    // Only use /Ox with /GL to prevent too long build times
                    if (Environment.WholeProgramOptimization)
                    {
                        // Enable Most Speed Optimizations
                        CommonArgs.Add("/Ox");

                        // Whole Program Optimization
                        CommonArgs.Add("/GL");
                    }
                    else
                    {
                        // Enable SEE2 and other code optimizations but without breaking incremental linking
                        //CommonArgs.Add("/Og"); // Results in D9035 warning
                        CommonArgs.Add("/O2");
                    }
                }
                else
                {
                    // Disable compiler optimizations (Debug)
                    CommonArgs.Add("/Od");

                    // Frame-Pointer Omission
                    CommonArgs.Add("/Oy-");
                }

                // Full Path of Source Code File in Diagnostics
                CommonArgs.Add("/FC"); 

                // Report Internal Compiler Errors
                CommonArgs.Add("/errorReport:prompt");

                // Exception Handling Model
                //if (!Environment.CompileAsWinRT)
                {
                    if (Environment.EnableExceptions)
                        CommonArgs.Add("/EHsc");
                    else
                        CommonArgs.Add("/D_HAS_EXCEPTIONS=0");
                }

                // Eliminate Duplicate Strings
                if (Environment.StringPooling)
                    CommonArgs.Add("/GF");
                else
                    CommonArgs.Add("/GF-");

                // Use Run-Time Library
                if (Environment.UseDebugCRT)
                    CommonArgs.Add("/MDd");
                else
                    CommonArgs.Add("/MD");

                // Specify floating-point behavior
                CommonArgs.Add("/fp:fast");
                CommonArgs.Add("/fp:except-");



                // Treats all compiler warnings as errors
                if (Environment.TreatWarningsAsErrors)
                    CommonArgs.Add("/WX");
                else
                    CommonArgs.Add("/WX-");

                // Show warnings
                CommonArgs.Add("/W3");

                // Silence macro redefinition warning
                CommonArgs.Add("/wd\"4005\"");

                // wchar_t is Native Type
                CommonArgs.Add("/Zc:wchar_t");

                // Common Language Runtime Compilation
                //if (Environment.CompileAsWinRT)
                //CommonArgs.Add("/clr");
                // Preprocessor conformance mode
                CommonArgs.Add("/Zc:preprocessor");

            }

            foreach (var definition in Environment.PreprocessorDefinitions)
                CommonArgs.Add(string.Format("/D \"{0}\"", definition));

            foreach (var includePath in SystemIncludePaths)
                AddIncludePath(CommonArgs, includePath, true);



            foreach (var includePath in Environment.IncludePaths)
                AddIncludePath(CommonArgs, includePath, false);



            string? pchFile = null, pchSource = null, pchPdbFile = null;

            var CompileArgs = new List<string>();
            if (Environment.PrecompiledHeaderUsage == PrecompiledHeaderFileUsage.CreateManual)
            {

                string pchHeaderFileLocation = Path.GetFullPath(Environment.PrecompiledHeaderSource);
                string? pchHeaderFileLocationDirectory = Path.GetDirectoryName(pchHeaderFileLocation);
                if(pchHeaderFileLocationDirectory == null)
                {
                    throw new Exception("Invalid PCH header file location");
                }
                string pchSourcePath = Path.Combine(pchHeaderFileLocationDirectory, Path.GetFileNameWithoutExtension(Environment.PrecompiledHeaderSource) + ".cpp");

                string pchFileName = Path.GetFileName(pchHeaderFileLocation);
                pchPdbFile = Path.Combine(outputPath, Path.ChangeExtension(pchFileName, "pdb"));
                string pchObjFile = Path.Combine(outputPath, Path.ChangeExtension(pchFileName, "obj"));

                if (!File.Exists(pchSourcePath))
                {
                    File.WriteAllText(pchSourcePath, $"#include \"{pchHeaderFileLocation}\"\n");
                }
                List<string> PrerequisiteFiles = new List<string>();
                List<string> ProducedFiles = new List<string>();
                pchFile = Path.Combine(outputPath, Path.ChangeExtension(pchFileName, "pch"));
                pchSource = pchHeaderFileLocation;

                PrerequisiteFiles.Add(pchHeaderFileLocation);
                PrerequisiteFiles.Add(pchSource);
                PrerequisiteFiles.AddRange(CppIncludeParser.GetAllHeaderDependencies(Environment.IncludePaths, pchSource));
                PrerequisiteFiles.AddRange(Environment.AdditionalPrerequisites);
                Output.PrecompiledHeaderFile = pchFile;
                Output.ObjectFiles.Add(pchObjFile);
                ProducedFiles.Add(pchObjFile);
                ProducedFiles.Add(pchPdbFile);
                ProducedFiles.Add(pchFile);


                CompileArgs.AddRange(CommonArgs);
                AppendArgs_CPP(Environment, CompileArgs);
                CompileArgs.Add(string.Format("/Yc\"{0}\"", pchSource));
                CompileArgs.Add(string.Format("/Fp\"{0}\"", pchFile));
                CompileArgs.Add(string.Format("/Fd\"{0}\"", pchPdbFile));
                CompileArgs.Add(string.Format("/Fo\"{0}\"", pchObjFile));
                CompileArgs.Add("/FS");
                CompileArgs.Add(string.Format("\"{0}\"", pchSourcePath));

                Executor.AddCommand(new CommandContext(CompilerPath!, string.Join(" ", CompileArgs), CommandType.GereratePCH,
                    ProducedFiles, PrerequisiteFiles, true, $"Compile {Path.GetFileName(pchSourcePath)} Generaring PCH File..."));

            }
            else if (Environment.PrecompiledHeaderUsage == PrecompiledHeaderFileUsage.UseManual)
            {
                string pchHeaderFileLocation = Path.GetFullPath(Environment.PrecompiledHeaderSource);

                string pchFileName = Path.GetFileName(pchHeaderFileLocation);
                pchPdbFile = Path.Combine(outputPath, Path.ChangeExtension(pchFileName, "pdb"));
                pchFile = Path.Combine(outputPath, Path.ChangeExtension(pchFileName, "pch"));
                pchSource = pchHeaderFileLocation;

                string pchObjFile = Path.Combine(outputPath, Path.ChangeExtension(pchFileName, "obj"));
                Output.ObjectFiles.Add(pchObjFile);
                Output.PrecompiledHeaderFile = pchFile;

            }


            bool UseSeparatePdb = true;

            if (pchFile != null)
            {
                // Include PCH file
                CompileArgs.Add(string.Format("/FI\"{0}\"", pchSource));
                CompileArgs.Add(string.Format("/Yu\"{0}\"", pchSource));
                CompileArgs.Add(string.Format("/Fp\"{0}\"", pchFile));
            }
            foreach (var sourceFile in sourceFiles)
            {
                List<string> PrerequisiteFiles = new List<string>();
                List<string> ProducedFiles = new List<string>();

                //var Headers = GetHeaderDependencies(sourceFile);
                var sourceFilename = Path.GetFileNameWithoutExtension(sourceFile);
                var objFile = Path.Combine(outputPath, sourceFilename + ".obj");
                
                Output.ObjectFiles.Add(objFile);
                ProducedFiles.Add(objFile);


                CompileArgs.Clear();
                bool IsCFile = Path.GetExtension(sourceFile).Equals(".c", StringComparison.OrdinalIgnoreCase);
                if (IsCFile)
                {
                    AppendArgs_C(Environment, CompileArgs);
                }
                else
                {

                    AppendArgs_CPP(Environment, CompileArgs);

                }
                CompileArgs.AddRange(CommonArgs);

                if (Environment.DebugInformation)
                {
                    // Program Database File Name
                    string? pdbFile = null;

                    if (pchFile != null && !IsCFile)
                    {
                        pdbFile = pchPdbFile;
                        if (UseSeparatePdb)
                            CompileArgs.Add("/FS");

                    }
                    else if (UseSeparatePdb)
                    {
                        pdbFile = Path.Combine(outputPath, sourceFilename + ".pdb");
                    }

                    if (pdbFile != null)
                    {
                        CompileArgs.Add(string.Format("/Fd\"{0}\"", pdbFile));
                    }
                }


                CompileArgs.Add(string.Format("/Fo\"{0}\"", objFile));
                CompileArgs.Add("\"" + sourceFile + "\"");

                PrerequisiteFiles.Add(sourceFile);
                
                // Use dependency cache for faster incremental builds
                var Includes = depCache.GetDependencies(sourceFile, Environment.IncludePaths);
                PrerequisiteFiles.AddRange(Includes);

                if (pchFile != null)
                {
                    PrerequisiteFiles.Add(pchFile);
                }
                
                PrerequisiteFiles.AddRange(Environment.AdditionalPrerequisites);
                
                // Use response file if command line is too long (Windows limit is ~32767 chars)
                string compilerArgs;
                if (string.Join(" ", CompileArgs).Length > 8000)
                {
                    var responseFile = Path.Combine(outputPath, sourceFilename + ".rsp");
                    var newContent = string.Join(System.Environment.NewLine, CompileArgs);
                    // Only rewrite if content changed to avoid unnecessary recompilation
                    if (!File.Exists(responseFile) || File.ReadAllText(responseFile) != newContent)
                        File.WriteAllText(responseFile, newContent);
                    compilerArgs = $"@\"{responseFile}\"";
                    PrerequisiteFiles.Add(responseFile);
                }
                else
                {
                    compilerArgs = string.Join(" ", CompileArgs);
                }
                
                Executor.AddCommand(new CommandContext(CompilerPath!, compilerArgs, CommandType.Compile,
                    ProducedFiles, PrerequisiteFiles, true, $"Compiling {Path.GetFileName(sourceFile)} .."));
            
            
            }
            
            // Save dependency cache after processing all files
            depCache.Save();
            
            return Output;
        }

        public override void ArchiveFiles(CommandExecutor executor, List<string> objects, string outputPath)
        {
            Directory.CreateDirectory(Path.GetDirectoryName(outputPath)!);
            string args = "/NOLOGO /OUT:\"" + outputPath + "\" " + string.Join(" ", objects.Select(x => "\"" + x + "\""));
            executor.AddCommand(new CommandContext(LibToolPath!, args, CommandType.Link,
                new List<string> { outputPath }, objects, false, "Archiving Beast.lib..."));
        }

        public override void LinkFiles(CommandExecutor Executor, LinkEnvironment LinkEnv, string outputFilePath)
        {

            List<string> PrerequisiteFiles = new List<string>();
            List<string> ProducedFiles = new List<string>();

            // Add all input .obj files as prerequisites so linker re-runs when any .obj is recompiled
            foreach (var inputFile in LinkEnv.InputFiles)
            {
                PrerequisiteFiles.Add(inputFile);
            }

            var args = new List<string>();

                string ExtentionFile = "";

                args.AddRange(LinkEnv.CustomArgs);
                {
                    // Suppress startup banner
                    args.Add("/NOLOGO");

                    // Report internal compiler errors
                    args.Add("/ERRORREPORT:PROMPT");

                    // Output File Name

                    if (LinkEnv.Output == LinkerOutput.Executable)
                        ExtentionFile = ".exe";
                    else if (LinkEnv.Output == LinkerOutput.SharedLibrary)
                        ExtentionFile = ".dll";
                    else if (LinkEnv.Output == LinkerOutput.StaticLibrary || LinkEnv.Output == LinkerOutput.ImportLibrary)
                        ExtentionFile = ".lib";


                    args.Add(string.Format("/OUT:\"{0}\"", outputFilePath + ExtentionFile));

                    // Specify target platform
                    switch (Architecture)
                    {
                        case PlatformArch.X64:
                            args.Add("/MACHINE:x64");
                            break;
                        case PlatformArch.ARM64:
                            args.Add("/MACHINE:ARM64");
                            break;
                        default: throw new InvalidArchitectureException(Architecture);
                    }

                    // Specify subsystem
                    if (LinkEnv.LinkAsConsoleProgram)
                    {
                        args.Add("/SUBSYSTEM:CONSOLE");
                    }
                    else
                    {
                        args.Add("/SUBSYSTEM:WINDOWS");
                    }


                    if (LinkEnv.LinkTimeCodeGeneration)
                    {
                        // Link-time code generation
                        args.Add("/LTCG");
                    }

                    if (LinkEnv.Output == LinkerOutput.ImportLibrary)
                    {
                        // Create an import library
                        args.Add("/DEF");

                        // Ignore libraries
                        foreach (string ExcludedLibrary in LinkEnv.ExcludedLibraries)
                        {
                            args.Add($"/NODEFAULTLIB:\"{ExcludedLibrary}\"");
                        }

                        // Specify the name
                        args.Add(string.Format("/NAME:\"{0}\"", Path.GetFileNameWithoutExtension(outputFilePath)));

                        // Ignore warnings about files with no public symbols
                        args.Add("/IGNORE:4221");
                    }
                    else
                    {
                        // Don't create Side-by-Side Assembly Manifest
                        args.Add("/MANIFEST:NO");

                        // Fixed Base Address
                        args.Add("/FIXED:NO");

                        // Compatible with Data Execution Prevention
                        args.Add("/NXCOMPAT");

                        // Allow delay-loaded DLLs to be explicitly unloaded
                        args.Add("/DELAY:UNLOAD");

                        if (LinkEnv.Output == LinkerOutput.SharedLibrary)
                        {
                            // Build a DLL
                            args.Add("/DLL");
                        }

                        // Redirect imports LIB file auto-generated for EXE/DLL
                        var libFile = Path.ChangeExtension(outputFilePath, StaticLibraryFileExtension);
                        args.Add("/IMPLIB:\"" + libFile + "\"");

                        // Don't embed the full PDB path
                        args.Add("/PDBALTPATH:%_PDB%");

                        // Optimize
                        if (LinkEnv.Optimization && !LinkEnv.UseIncrementalLinking)
                        {
                            // Generate an EXE checksum
                            args.Add("/RELEASE");

                            // Eliminate unreferenced symbols
                            args.Add("/OPT:REF");

                            // Remove redundant COMDATs
                            args.Add("/OPT:ICF");
                        }
                        else
                        {
                            // Keep symbols that are unreferenced
                            args.Add("/OPT:NOREF");

                            // Disable identical COMDAT folding
                            args.Add("/OPT:NOICF");
                        }

                        // Link Incrementally
                        if (LinkEnv.UseIncrementalLinking && !LinkEnv.LinkTimeCodeGeneration)
                        {
                            args.Add("/INCREMENTAL");
                        }
                        else
                        {
                            args.Add("/INCREMENTAL:NO");
                        }

                        if (LinkEnv.DebugInformation)
                        {
                            // Generate debug information
                            if (LinkEnv.UseFastPDBLinking)
                            {
                                args.Add("/DEBUG:FASTLINK");
                            }
                            else if (LinkEnv.UseFullDebugInformation)
                            {
                                args.Add("/DEBUG:FULL");
                            }
                            else
                            {
                                args.Add("/DEBUG"); // Same as /DEBUG:FULL
                            }

                            // Use Program Database
                            var pdbFile = Path.ChangeExtension(outputFilePath, ".pdb");
                            args.Add(string.Format("/PDB:\"{0}\"", pdbFile));
                            ProducedFiles.Add(pdbFile);
                        }
                    }


                }

                // Delay-load DLLs
                if (LinkEnv.Output == LinkerOutput.Executable || LinkEnv.Output == LinkerOutput.SharedLibrary)
                {
                    foreach (var dll in LinkEnv.DelayLoadLibraries)
                        args.Add(string.Format("/DELAYLOAD:\"{0}\"", dll));
                }

                // Additional lib paths
                foreach (var libpath in SystemLibraryPaths)
                {
                    args.Add(string.Format("/LIBPATH:\"{0}\"", libpath));
                }
                foreach (var libpath in LinkEnv.LibraryPaths)
                {
                    args.Add(string.Format("/LIBPATH:\"{0}\"", libpath));
                }

                // Input libraries
                foreach (var library in LinkEnv.InputLibraries)
                {
                    args.Add(string.Format("\"{0}\"", library));
                    if (File.Exists(library)) PrerequisiteFiles.Add(library);
                }

                // Input files
                foreach (var file in LinkEnv.InputFiles)
                {
                    if (file.EndsWith(".pch", StringComparison.OrdinalIgnoreCase))
                    {
                        // PCH file
                        args.Add(string.Format("/Yu:\"{0}\"", file));
                        continue;
                    }

                    args.Add(string.Format("\"{0}\"", file));
                }
                bool useResponseFile = true;
                string? responseFile = null;
                if (useResponseFile)
                {
                    string intermediateDir = (GlobalEnvironment.IsProjectBuild && !string.IsNullOrEmpty(GlobalEnvironment.ProjectPath))
                        ? Path.Combine(GlobalEnvironment.ProjectPath, "Intermediate")
                        : GlobalEnvironment.IntermediatePath;
                    if (!Directory.Exists(intermediateDir))
                        Directory.CreateDirectory(intermediateDir);
                    responseFile = Path.Combine(intermediateDir, Path.GetFileName(outputFilePath) + ".response");
                    var newContent = string.Join(Environment.NewLine, args);
                    // Only rewrite if content changed to avoid unnecessary relinking
                    if (!File.Exists(responseFile) || File.ReadAllText(responseFile, new UTF8Encoding()) != newContent)
                        File.WriteAllText(responseFile, newContent, new UTF8Encoding());
                    PrerequisiteFiles.Add(responseFile);
            }
            ProducedFiles.Add(outputFilePath + ExtentionFile);
            Executor.AddCommand(new CommandContext(LinkEnv.Output == LinkerOutput.ImportLibrary ? LibToolPath! : LinkerPath!,
                            useResponseFile ? string.Format("@\"{0}\"", responseFile) : string.Join(" ", args),
                            CommandType.Link,
                            ProducedFiles, 
                            PrerequisiteFiles,
                            false,
                            $"Linking files for {Path.GetFileName(outputFilePath) + ExtentionFile}..."));
            
        }

        protected void AddIncludePath(List<string> Args, string Path, bool bIsSystemInclude)
        {
            string Prefix = bIsSystemInclude ? "/external:I " : "/I ";
            if (!Args.Contains("/external:W0"))
                Args.Add("/external:W0");

            if (Path.Contains(' '))
                Args.Add(string.Format("{0}\"{1}\"", Prefix, Path));
            else
                Args.Add(string.Format("{0}{1}", Prefix, Path));
        }



        public static bool TryGetLatestWindowsSdkDir(out string? sdkDir, out List<Version>? sdkVersion)
        {
            sdkDir = null;
            sdkVersion = null;

            const string registryKeyPath = @"Microsoft\\Windows Kits\\Installed Roots";
            const string registryValueName = "KitsRoot10";

            if (TryReadInstallDirRegistryKey32(registryKeyPath, registryValueName, out sdkDir))
            {
                if (Directory.Exists(sdkDir))
                {
                    var includeRootDir = Path.Combine(sdkDir, "Include");
                    sdkVersion = new List<Version>();
                    foreach (var includeDir in Directory.GetDirectories(includeRootDir))
                    {
                        if (Version.TryParse(Path.GetFileName(includeDir), out var version) && File.Exists(Path.Combine(includeDir, "um", "windows.h")))
                        {
                            sdkVersion.Add(version);
                        }
                    }
                    return true;
                }
            }
            return false;
        }
        public static bool TryReadDirRegistryKey(string keyName, string valueName, out string? value)
        {
            if (!OperatingSystem.IsWindows())
            {
                value = null;
                return false;
            }

            value = Registry.GetValue(keyName, valueName, null) as string;
            if (string.IsNullOrEmpty(value))
            {
                value = null;
                return false;
            }
            return true;
        }
        public static bool TryReadInstallDirRegistryKey32(string keySuffix, string valueName, out string? dir)
        {
            if (TryReadDirRegistryKey("HKEY_CURRENT_USER\\SOFTWARE\\" + keySuffix, valueName, out dir))
                return true;
            if (TryReadDirRegistryKey("HKEY_LOCAL_MACHINE\\SOFTWARE\\" + keySuffix, valueName, out dir))
                return true;
            if (TryReadDirRegistryKey("HKEY_CURRENT_USER\\SOFTWARE\\Wow6432Node\\" + keySuffix, valueName, out dir))
                return true;
            if (TryReadDirRegistryKey("HKEY_LOCAL_MACHINE\\SOFTWARE\\Wow6432Node\\" + keySuffix, valueName, out dir))
                return true;
            return false;
        }
    }

}
