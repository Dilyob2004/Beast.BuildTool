using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Text;
using System.Threading;
using System.Threading.Tasks;

namespace Beast.BuildTool
{
    public static class CommandUtilities
    {
        public static string ReadProcessOutput(string filename, string ?args = null)
        {
            Process p = new Process
            {
                StartInfo =
                {
                    FileName = filename,
                    Arguments = args,
                    UseShellExecute = false,
                    CreateNoWindow = true,
                    RedirectStandardOutput = true,
                }
            };
            p.Start();
            return p.StandardOutput.ReadToEnd().Trim();
        }
    }
    public enum CommandType
    {
        None = 0,
        Link,
        Compile,
        GereratePCH
    }

    public class CommandContext
    {
        public string Id { get; } = Guid.NewGuid().ToString();
        public CommandType Type { get; } = CommandType.None;
        public CommandStatus Status { get; private set; } = CommandStatus.Pending;

        public IReadOnlyList<string> Output { get; private set; } = new List<string>();
        public bool Async { get; } = true;
        public bool HasErrors { get; } = false;
        public List<string> PrerequisiteFiles { get; } = new List<string>();
        public List<string> ProducedFiles { get; } = new List<string>();
        public string Description { get; } = "";

        public string CompilerPath { get; }
        public string Arguments { get; }

        public async Task ExecuteAsync(CancellationToken cancellationToken = default)
        {

            Status = CommandStatus.Running;

            try
            {
                bool RequireProcess = false;
                string? RecompileReason = null;

                // Check if any produced file is missing
                foreach (var ProducedFile in ProducedFiles)
                {
                    if (!File.Exists(ProducedFile))
                    {
                        RequireProcess = true;
                        RecompileReason = $"Output missing: {Path.GetFileName(ProducedFile)}";
                        break;
                    }
                }

                // If all outputs exist, check timestamps
                if (!RequireProcess)
                {
                    // Find the oldest produced file
                    DateTime oldestProducedTime = DateTime.MaxValue;
                    string? oldestProducedFile = null;
                    
                    foreach (var ProducedFile in ProducedFiles)
                    {
                        DateTime producedTime = File.GetLastWriteTimeUtc(ProducedFile);
                        if (producedTime < oldestProducedTime)
                        {
                            oldestProducedTime = producedTime;
                            oldestProducedFile = ProducedFile;
                        }
                    }

                    // Check if any prerequisite is newer than the oldest output
                    foreach (var PrerequisiteFile in PrerequisiteFiles)
                    {
                        if (!File.Exists(PrerequisiteFile))
                            continue;
                            
                        DateTime prerequisiteTime = File.GetLastWriteTimeUtc(PrerequisiteFile);
                        if (prerequisiteTime > oldestProducedTime)
                        {
                            RequireProcess = true;
                            RecompileReason = $"Source modified: {Path.GetFileName(PrerequisiteFile)}";
                            break;
                        }
                    }
                }

                if (!RequireProcess)
                {
                    Status = CommandStatus.Completed;
                    return;
                }

                // Log the reason for rebuild/relink
                if (Type == CommandType.Link)
                    Log.TraceInfo($"Relinking: {RecompileReason}");
                else
                    Log.TraceVerbose($"Rebuild reason: {RecompileReason}");
                string cmdCommand = $"\"{CompilerPath}\" {Arguments}";
                string cmdArguments = $"/c \"{cmdCommand}\"";

                Log.TraceInfo(Description);

                var processInfo = new ProcessStartInfo
                {
                    FileName = "cmd.exe",
                    Arguments = cmdArguments,
                    UseShellExecute = false,
                    RedirectStandardOutput = true,
                    RedirectStandardError = true,
                    CreateNoWindow = true,
                    WorkingDirectory = GlobalEnvironment.WorkingDirectory
                };

                processInfo.EnvironmentVariables["VSLANG"] = "1033";

                var process = new Process { StartInfo = processInfo };

                process.Start();


                string output = process.StandardOutput.ReadToEnd();
                string errors = process.StandardError.ReadToEnd();


                await process.WaitForExitAsync(cancellationToken);

                if (process.ExitCode != 0)
                {
                    List<string> Result = new List<string>();
                    //Result.Add($"Result Compilation {CurrentFileNameCpp} file: ");
                    if (output.Length > 0) Result.Add(output);
                    if (errors.Length > 0) Result.Add(errors);

                    Output = Result;

                    Status = CommandStatus.Failed;
                    Log.TraceError(string.Join(" ", Output));
                    return;
                }
                Status = CommandStatus.Completed;
            }
            catch (OperationCanceledException)
            {
                Status = CommandStatus.Canceled;
                Output = new List<string> { "Operation was canceled" };
            }
            catch (Exception ex)
            {
                Status = CommandStatus.Failed;
                Output = new List<string> { ex.Message };
            }
        }
        public CommandContext(
            string compilerPath,
            string arguments,
            CommandType type,
            List<string> producedFiles,
            List<string> prerequisiteFiles,
            bool async,
            string Desc)
        {
            ProducedFiles = producedFiles ?? new List<string>();
            PrerequisiteFiles = prerequisiteFiles ?? new List<string>();

            CompilerPath = compilerPath;
            Arguments = arguments;
            Description = Desc;
            Async = async;
            Type = type;
        }


    }

    public enum CommandStatus
    {
        Pending,
        Running,
        Completed,
        Failed,
        Canceled
    }

    public static class ProcessExtensions
    {
        public static Task WaitForExitAsync(
            this Process process,
            CancellationToken cancellationToken = default)
        {
            var tcs = new TaskCompletionSource<bool>();
            process.EnableRaisingEvents = true;
            process.Exited += (s, e) => tcs.TrySetResult(true);

            cancellationToken.Register(() =>
            {
                tcs.TrySetCanceled();
                try { process.Kill(); } catch { /* Ignore */ }
            });

            return tcs.Task;
        }
    }


    public class CommandExecutor
    {
        private readonly ConcurrentQueue<CommandContext> _commands = new ConcurrentQueue<CommandContext>();
        private readonly ConcurrentQueue<Task> _additionalTasks = new ConcurrentQueue<Task>();
        private readonly SemaphoreSlim _semaphore;
        private CancellationTokenSource? _cts;

        public CommandExecutor(int maxDegreeOfParallelism = 0)
        {
            _semaphore = new SemaphoreSlim(
                maxDegreeOfParallelism <= 0 ? Environment.ProcessorCount : maxDegreeOfParallelism
            );
        }

        public void AddCommand(CommandContext command) => _commands.Enqueue(command);
        public void AddTask(Task task) => _additionalTasks.Enqueue(task);

        public async Task ExecuteAsync(CancellationToken externalToken = default)
        {
            Log.TraceVerbose(string.Format("Checking {0} tasks", _commands.Count));
            _cts = CancellationTokenSource.CreateLinkedTokenSource(externalToken);
            var tasks = new List<Task>();




            foreach (var command in _commands
                .Where(c => c.Type == CommandType.GereratePCH)
                .ToList())
            {
                await _semaphore.WaitAsync(_cts.Token);
                if (command.Async)
                {
                    tasks.Add(Task.Run(async () =>
                    {
                        try
                        {
                            await command.ExecuteAsync(_cts.Token);
                        }
                        finally
                        {
                            _semaphore.Release();
                        }
                    }, _cts.Token));

                }
                else
                {
                    try { await command.ExecuteAsync(_cts.Token); }
                    finally { _semaphore.Release(); }
                }

            }
            await Task.WhenAll(tasks);
            ThrowIfFailed();

            tasks.Clear();
            foreach (var command in _commands
                .Where(c => c.Type == CommandType.Compile)
                .ToList())
            {
                await _semaphore.WaitAsync(_cts.Token);
                if (command.Async)
                {
                    tasks.Add(Task.Run(async () =>
                    {
                        try
                        {
                            await command.ExecuteAsync(_cts.Token);
                        }
                        finally
                        {
                            _semaphore.Release();
                        }
                    }, _cts.Token));

                }
                else
                {
                    try { await command.ExecuteAsync(_cts.Token); }
                    finally { _semaphore.Release(); }
                }
            }

            await Task.WhenAll(tasks);
            ThrowIfFailed();

            /*tasks.Clear();
            foreach(var additionalTask in _additionalTasks)
            {
                tasks.Add(additionalTask);
            }
            await Task.WhenAll(tasks);
            */


            foreach (var command in _commands.Where(c => c.Type == CommandType.Link) .ToList())
            {
                await command.ExecuteAsync(_cts.Token);

                if (command.Status == CommandStatus.Failed)
                {
                    throw new InvalidOperationException("Build failed during linking/archiving. See the compiler output above.");
                }
            }
            var failedCount = 0;
            foreach (var Command in _commands)
            {
                if (Command.Status == CommandStatus.Failed)
                {
                    failedCount++;
                }
            }
            if (failedCount == 1)
                Log.TraceError("1 task failed");
            else if (failedCount != 0)
                Log.TraceError(string.Format("{0} tasks failed", failedCount));
            else
                Log.TraceInfo("Done!");
        }

        public void Cancel() => _cts?.Cancel();

        private void ThrowIfFailed()
        {
            int count = _commands.Count(command => command.Status == CommandStatus.Failed);
            if (count != 0) throw new InvalidOperationException($"Build failed: {count} command(s) failed. Linking was skipped.");
        }

        public IReadOnlyList<CommandContext> GetResults() => _commands.ToList();
    }

}
