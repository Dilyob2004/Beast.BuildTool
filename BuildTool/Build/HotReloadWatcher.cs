using System.Diagnostics;

namespace Beast.BuildTool;

internal static class HotReloadWatcher
{
    internal static async Task Run(string project, string platformName, string? avd, string? device, bool prepareOnly)
    {
        if (!platformName.Equals("Windows", StringComparison.OrdinalIgnoreCase) && !platformName.Equals("Android", StringComparison.OrdinalIgnoreCase))
            throw new ArgumentException("watch supports Windows and Android.");
        bool android = platformName.Equals("Android", StringComparison.OrdinalIgnoreCase);
        string lockRoot = Path.Combine(project, "Intermediate", "HotReload"); Directory.CreateDirectory(lockRoot);
        using var sessionLock = new FileStream(Path.Combine(lockRoot, android ? "Android.lock" : "Windows.lock"), FileMode.OpenOrCreate, FileAccess.ReadWrite, FileShare.None);
        string adb = Path.Combine(BuildConfigManager.Config.Android.SdkRoot, "platform-tools", "adb.exe");
        string? serial = null;
        Platform platform;
        if (android)
        {
            var config = BuildConfigManager.Config.Android;
            serial = await AndroidRunner.ResolveDeviceAsync(adb, Path.Combine(config.SdkRoot, "emulator", "emulator.exe"), config, avd, device);
            await AndroidRunner.WaitForBootAsync(adb, serial, config.EmulatorBootTimeoutSeconds);
            string abi = (await HotReloadBuilder.Execute(adb, ["-s", serial, "shell", "getprop", "ro.product.cpu.abi"])).Trim();
            if (abi is not ("x86_64" or "arm64-v8a")) throw new InvalidOperationException("Unsupported emulator ABI: " + abi);
            platform = new AndroidPlatform(abi == "x86_64");
        }
        else platform = new WindowsPlatform();
        var build = new HotReloadBuilder(project, platform);
        Log.TraceInfo("Hot reload session: " + build.Root);
        string stamp = build.SourceStamp();
        await build.BuildRuntime(); await build.BuildModule("initial"); await build.BuildHost();
        string? activityPackage = android ? await build.PackageHost() : null;
        if (prepareOnly) { Log.TraceInfo("Development host prepared; no watch loop started."); return; }
        string package = "com.beast." + build.Name.ToLowerInvariant() + ".hot";
        Process? host = null;
        string pid;
        if (android)
        {
            await HotReloadBuilder.Execute(adb, ["-s", serial!, "install", "-r", Path.Combine(build.Root, build.Name + ".HotReload.apk")]);
            await HotReloadBuilder.Execute(adb, ["-s", serial!, "shell", "am", "force-stop", package]);
            // Remove only the previous dev-session mailbox. Old module files may
            // remain for inspection; the new host never loads them implicitly.
            await HotReloadBuilder.Execute(adb, ["-s", serial!, "shell", "run-as", package, "rm", "-f", "files/beast-hot/request", "files/beast-hot/status"]);
            await HotReloadBuilder.Execute(adb, ["-s", serial!, "shell", "am", "start", "-W", "-n", package + "/" + activityPackage + ".GameActivity"]);
            pid = (await HotReloadBuilder.Execute(adb, ["-s", serial!, "shell", "pidof", package])).Trim();
        }
        else
        {
            host = Process.Start(new ProcessStartInfo(Path.Combine(build.Root, "BeastHost.exe")) { WorkingDirectory = build.Root, UseShellExecute = false })!;
            pid = host.Id.ToString();
        }
        Log.TraceInfo($"Hot reload ready; host PID={pid}. Save C++/headers to reload. Ctrl+C stops watching; close the app to exit.");
        using var cancelled = new CancellationTokenSource();
        ConsoleCancelEventHandler handler = (_, e) => { e.Cancel = true; cancelled.Cancel(); };
        Console.CancelKeyPress += handler;
        try
        {
            string? observed = null, pending = null; int stable = 0;
            while (!cancelled.IsCancellationRequested)
            {
                await Task.Delay(500, cancelled.Token);
                if (host?.HasExited == true) {
                    if (host.ExitCode != 0) throw new InvalidOperationException("Hot reload host exited with code " + host.ExitCode);
                    break;
                }
                if (android)
                {
                    try { if ((await HotReloadBuilder.Execute(adb, ["-s", serial!, "shell", "pidof", package])).Trim() != pid) break; }
                    catch (InvalidOperationException) { break; }
                }
                if (pending != null)
                {
                    string status = "";
                    try { status = android ? await HotReloadBuilder.Execute(adb, ["-s", serial!, "shell", "run-as", package, "cat", "files/beast-hot/status"]) : File.ReadAllText(Path.Combine(build.Root, "status")); }
                    catch (IOException) { }
                    catch (InvalidOperationException) { }
                    var lines = status.Split('\n');
                    if (lines.Length >= 2 && lines[0].Trim() == pending) { Log.TraceInfo("Host: " + status.Trim().Replace('\n', ' ')); pending = null; }
                    // Do not overwrite a candidate until the foreground host
                    // has acknowledged it. Edits while backgrounded accumulate.
                    else continue;
                }
                build.CheckInfrastructure();
                string current;
                try { current = build.SourceStamp(); }
                catch (IOException) { stable = 0; continue; } // Editor atomic save in progress.
                if (current == stamp) { observed = null; stable = 0; continue; }
                if (current != observed) { observed = current; stable = 0; continue; }
                if (++stable < 2) continue;
                stamp = current; stable = 0;
                try
                {
                    string module = await build.BuildModule(Guid.NewGuid().ToString("N"));
                    if (cancelled.IsCancellationRequested) break;
                    if (build.SourceStamp() != stamp) { Log.TraceInfo("Source changed during build; discarding stale candidate."); stamp = ""; continue; }
                    string name = Path.GetFileName(module);
                    if (android) await Deploy(adb, serial!, package, module);
                    else { File.WriteAllText(Path.Combine(build.Root, "request.tmp"), name + "\n"); File.Move(Path.Combine(build.Root, "request.tmp"), Path.Combine(build.Root, "request"), true); }
                    pending = name;
                    Log.TraceInfo("Candidate delivered; waiting for frame-boundary acknowledgement: " + name);
                }
                catch (Exception error) { Log.TraceError("Reload build failed; last-good scene remains active. " + error.Message); }
            }
        }
        catch (OperationCanceledException) when (cancelled.IsCancellationRequested) { }
        finally { Console.CancelKeyPress -= handler; host?.Dispose(); }
    }
    private static async Task Deploy(string adb, string serial, string package, string module)
    {
        string name = Path.GetFileName(module), remote = "/data/local/tmp/" + name;
        await HotReloadBuilder.Execute(adb, ["-s", serial, "push", module, remote]);
        try
        {
            // Open first, mark read-only before writing, publish the mailbox last.
            // run-as confines executable code to this debuggable app's private data.
            string script = $"umask 077; mkdir -p files/beast-hot && exec 3>files/beast-hot/{name} && chmod 400 files/beast-hot/{name} && cat {remote} >&3 && exec 3>&-";
            await HotReloadBuilder.Execute(adb, ["-s", serial, "shell", "run-as", package, "sh", "-c", "'" + script + "'"]);
            using var source = File.OpenRead(module);
            string expected = Convert.ToHexString(System.Security.Cryptography.SHA256.HashData(source));
            string actual = await HotReloadBuilder.Execute(adb, ["-s", serial, "shell", "run-as", package, "sha256sum", "files/beast-hot/" + name]);
            if (!actual.Split(' ', StringSplitOptions.RemoveEmptyEntries)[0].Equals(expected, StringComparison.OrdinalIgnoreCase))
                throw new IOException("Transferred module SHA256 differs; candidate was not published.");
            script = $"umask 077; echo {name} >files/beast-hot/request.tmp && mv files/beast-hot/request.tmp files/beast-hot/request";
            await HotReloadBuilder.Execute(adb, ["-s", serial, "shell", "run-as", package, "sh", "-c", "'" + script + "'"]);
        }
        finally { await HotReloadBuilder.Execute(adb, ["-s", serial, "shell", "rm", "-f", remote]); }
    }
}
