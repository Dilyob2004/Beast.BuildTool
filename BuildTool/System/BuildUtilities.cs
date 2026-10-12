using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Text;
using System.Text.Json;

namespace Beast.BuildTool
{
    /// <summary>
    /// Generates compile_commands.json for IDE/clangd integration.
    /// </summary>
    public class CompileCommandsGenerator
    {
        private readonly List<CompileCommand> _commands = new List<CompileCommand>();
        private readonly string _workspacePath;

        private class CompileCommand
        {
            public string directory { get; set; } = "";
            public string file { get; set; } = "";
            public string command { get; set; } = "";
        }

        public CompileCommandsGenerator(string workspacePath)
        {
            _workspacePath = workspacePath;
        }

        /// <summary>
        /// Adds a compile command for a source file.
        /// </summary>
        public void AddCommand(string sourceFile, string compilerPath, IEnumerable<string> arguments)
        {
            var command = new CompileCommand
            {
                directory = _workspacePath,
                file = sourceFile,
                command = $"\"{compilerPath}\" {string.Join(" ", arguments)}"
            };
            
            _commands.Add(command);
        }

        /// <summary>
        /// Generates the compile_commands.json file.
        /// </summary>
        public void Generate()
        {
            if (_commands.Count == 0)
            {
                Log.TraceWarning("No compile commands to generate");
                return;
            }

            var outputPath = Path.Combine(_workspacePath, "compile_commands.json");
            
            try
            {
                var options = new JsonSerializerOptions
                {
                    WriteIndented = true
                };
                
                var json = JsonSerializer.Serialize(_commands, options);
                File.WriteAllText(outputPath, json);
                
                Log.TraceInfo($"Generated compile_commands.json with {_commands.Count} entries at {outputPath}");
            }
            catch (Exception ex)
            {
                Log.TraceError($"Failed to generate compile_commands.json: {ex.Message}");
            }
        }
    }

    /// <summary>
    /// Tracks and reports build statistics.
    /// </summary>
    public class BuildStatistics
    {
        private readonly Stopwatch _totalTimer = new Stopwatch();
        private readonly Dictionary<string, TimeSpan> _moduleTimes = new Dictionary<string, TimeSpan>();
        private readonly Dictionary<string, Stopwatch> _activeModuleTimers = new Dictionary<string, Stopwatch>();
        
        public int TotalFilesCompiled { get; private set; }
        public int FilesSkipped { get; private set; }
        public int FilesFailed { get; private set; }
        
        public TimeSpan TotalTime => _totalTimer.Elapsed;

        public void StartBuild()
        {
            _totalTimer.Start();
        }

        public void EndBuild()
        {
            _totalTimer.Stop();
        }

        public void StartModule(string moduleName)
        {
            var timer = new Stopwatch();
            timer.Start();
            _activeModuleTimers[moduleName] = timer;
        }

        public void EndModule(string moduleName)
        {
            if (_activeModuleTimers.TryGetValue(moduleName, out var timer))
            {
                timer.Stop();
                _moduleTimes[moduleName] = timer.Elapsed;
                _activeModuleTimers.Remove(moduleName);
            }
        }

        public void RecordCompiled() => TotalFilesCompiled++;
        public void RecordSkipped() => FilesSkipped++;
        public void RecordFailed() => FilesFailed++;

        public void PrintSummary()
        {
            Log.TraceInfo("=== Build Statistics ===");
            Log.TraceInfo($"Total time: {TotalTime.TotalSeconds:F2}s");
            Log.TraceInfo($"Files compiled: {TotalFilesCompiled}");
            Log.TraceInfo($"Files skipped (up-to-date): {FilesSkipped}");
            
            if (FilesFailed > 0)
            {
                Log.TraceError($"Files failed: {FilesFailed}");
            }

            if (_moduleTimes.Count > 0)
            {
                Log.TraceInfo("Module times:");
                
                // Sort by time descending
                var sorted = new List<KeyValuePair<string, TimeSpan>>(_moduleTimes);
                sorted.Sort((a, b) => b.Value.CompareTo(a.Value));
                
                foreach (var (module, time) in sorted)
                {
                    Log.TraceInfo($"  {module}: {time.TotalSeconds:F2}s");
                }
            }
        }
    }
}
