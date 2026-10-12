using System.Diagnostics;

namespace Beast.BuildTool;

/// <summary>Starts an Android emulator when needed, installs the APK, and launches the app activity.</summary>
internal static class AndroidRunner
{
    public static async Task RunAsync(
        string projectPath,
        string projectName,
        TargetConfiguration configuration,
        string? requestedAvd,
        string? requestedDevice,
        bool waitForExit)
    {
        string apk = Path.Combine(projectPath, "Binaries", "Android", configuration.ToString(), projectName + ".apk");
        if (!File.Exists(apk))
            throw new FileNotFoundException("Android APK was not found. Build the project first or omit --no-build.", apk);

        AndroidBuildConfig android = BuildConfigManager.Config.Android;
        string sdkRoot = Environment.ExpandEnvironmentVariables(android.SdkRoot);
        string adb = Path.Combine(sdkRoot, "platform-tools", "adb.exe");
        string emulator = Path.Combine(sdkRoot, "emulator", "emulator.exe");
        if (!File.Exists(adb)) throw new FileNotFoundException("Android Debug Bridge (adb.exe) was not found.", adb);
        if (!File.Exists(emulator)) throw new FileNotFoundException("Android Emulator was not found.", emulator);

        string serial = await ResolveDeviceAsync(adb, emulator, android, requestedAvd, requestedDevice);
        await WaitForBootAsync(adb, serial, android.EmulatorBootTimeoutSeconds);

        Log.TraceInfo($"Installing Android APK on {serial}...");
        await RunCheckedAsync(adb, ["-s", serial, "install", "-r", apk], "APK installation failed");

        string packageName = "com.beast." + projectName.ToLowerInvariant();
        string componentName = packageName + "/.GameActivity";
        await RunCheckedAsync(adb, ["-s", serial, "shell", "am", "force-stop", packageName], "Could not stop the previous Android app instance");
        await RunCheckedAsync(adb, ["-s", serial, "shell", "am", "start", "-n", componentName], "Could not start the Android app");
        Log.TraceInfo($"Android app started: {componentName} on {serial}.");

        if (waitForExit)
            await WaitForAppExitAsync(adb, serial, packageName);
    }

    internal static async Task<string> ResolveDeviceAsync(string adb, string emulator, AndroidBuildConfig android, string? requestedAvd, string? requestedDevice)
    {
        List<string> runningDevices = await GetEmulatorDevicesAsync(adb);
        if (!string.IsNullOrWhiteSpace(requestedDevice))
        {
            if (!runningDevices.Contains(requestedDevice, StringComparer.OrdinalIgnoreCase))
                throw new InvalidOperationException($"Android emulator '{requestedDevice}' is not connected. Connected emulators: {FormatDevices(runningDevices)}.");
            return requestedDevice;
        }

        if (runningDevices.Count == 1)
            return runningDevices[0];
        if (runningDevices.Count > 1)
            throw new InvalidOperationException($"Several Android emulators are connected ({FormatDevices(runningDevices)}). Select one with --device <serial>.");

        List<string> avds = await GetAvdsAsync(emulator);
        string avd = !string.IsNullOrWhiteSpace(requestedAvd) ? requestedAvd : android.DefaultAvd;
        if (string.IsNullOrWhiteSpace(avd))
        {
            if (avds.Count == 1)
                avd = avds[0];
            else
                throw new InvalidOperationException($"Several Android virtual devices are available ({FormatDevices(avds)}). Set android.defaultAvd in Config/BuildConfig.json or pass --avd <name>.");
        }
        if (!avds.Contains(avd, StringComparer.OrdinalIgnoreCase))
            throw new InvalidOperationException($"Android virtual device '{avd}' was not found. Available AVDs: {FormatDevices(avds)}.");

        Log.TraceInfo($"Starting Android emulator '{avd}'...");
        var startInfo = new ProcessStartInfo(emulator)
        {
            UseShellExecute = false,
            CreateNoWindow = true
        };
        startInfo.ArgumentList.Add("-avd");
        startInfo.ArgumentList.Add(avd);
        _ = Process.Start(startInfo) ?? throw new InvalidOperationException("Could not start Android Emulator.");
        return await WaitForNewEmulatorAsync(adb, android.EmulatorBootTimeoutSeconds);
    }

    private static async Task<List<string>> GetAvdsAsync(string emulator)
    {
        ProcessResult result = await RunProcessAsync(emulator, ["-list-avds"]);
        if (result.ExitCode != 0) throw new InvalidOperationException($"Could not list Android virtual devices:{Environment.NewLine}{result.Output}");
        return result.Output.Split(['\r', '\n'], StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries).ToList();
    }

    private static async Task<List<string>> GetEmulatorDevicesAsync(string adb)
    {
        ProcessResult result = await RunProcessAsync(adb, ["devices"]);
        if (result.ExitCode != 0) throw new InvalidOperationException($"Could not query adb devices:{Environment.NewLine}{result.Output}");
        return result.Output.Split(['\r', '\n'], StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries)
            .Skip(1)
            .Select(line => line.Split(['\t', ' '], StringSplitOptions.RemoveEmptyEntries))
            // An emulator reports "offline" while it is still booting. It is still
            // the right target: WaitForBootAsync will wait for it instead of trying
            // to launch a second instance of the same AVD.
            .Where(parts => parts.Length >= 2 && parts[0].StartsWith("emulator-", StringComparison.OrdinalIgnoreCase))
            .Select(parts => parts[0])
            .ToList();
    }

    private static async Task<string> WaitForNewEmulatorAsync(string adb, int timeoutSeconds)
    {
        DateTime deadline = DateTime.UtcNow.AddSeconds(NormalizeTimeout(timeoutSeconds));
        while (DateTime.UtcNow < deadline)
        {
            List<string> devices = await GetEmulatorDevicesAsync(adb);
            if (devices.Count == 1) return devices[0];
            if (devices.Count > 1) throw new InvalidOperationException($"Several Android emulators connected while starting the requested AVD: {FormatDevices(devices)}. Use --device <serial>.");
            await Task.Delay(TimeSpan.FromSeconds(1));
        }
        throw new TimeoutException("Timed out waiting for Android Emulator to connect to adb.");
    }

    internal static async Task WaitForBootAsync(string adb, string serial, int timeoutSeconds)
    {
        Log.TraceInfo($"Waiting for Android emulator {serial} to finish booting...");
        DateTime deadline = DateTime.UtcNow.AddSeconds(NormalizeTimeout(timeoutSeconds));
        while (DateTime.UtcNow < deadline)
        {
            ProcessResult result = await RunProcessAsync(adb, ["-s", serial, "shell", "getprop", "sys.boot_completed"]);
            if (result.ExitCode == 0 && result.Output.Trim() == "1") return;
            await Task.Delay(TimeSpan.FromSeconds(1));
        }
        throw new TimeoutException($"Timed out waiting for Android emulator {serial} to boot.");
    }

    private static async Task WaitForAppExitAsync(string adb, string serial, string packageName)
    {
        Log.TraceInfo("Android app is running. Close the app or emulator to finish this launch session.");
        while (true)
        {
            ProcessResult result = await RunProcessAsync(adb, ["-s", serial, "shell", "pidof", packageName]);
            if (result.ExitCode != 0)
            {
                if (!string.IsNullOrWhiteSpace(result.Output) &&
                    !result.Output.Contains("not found", StringComparison.OrdinalIgnoreCase))
                    Log.TraceInfo($"Android launch session ended: {result.Output.Trim()}");
                else
                    Log.TraceInfo("Android app was closed.");
                return;
            }

            if (string.IsNullOrWhiteSpace(result.Output))
            {
                Log.TraceInfo("Android app was closed.");
                return;
            }

            await Task.Delay(TimeSpan.FromSeconds(1));
        }
    }

    private static async Task RunCheckedAsync(string executable, IEnumerable<string> arguments, string errorMessage)
    {
        ProcessResult result = await RunProcessAsync(executable, arguments);
        if (result.ExitCode != 0) throw new InvalidOperationException($"{errorMessage}:{Environment.NewLine}{result.Output}");
        if (!string.IsNullOrWhiteSpace(result.Output)) Log.TraceVerbose(result.Output.Trim());
    }

    private static async Task<ProcessResult> RunProcessAsync(string executable, IEnumerable<string> arguments)
    {
        var startInfo = new ProcessStartInfo(executable)
        {
            UseShellExecute = false,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            CreateNoWindow = true
        };
        foreach (string argument in arguments) startInfo.ArgumentList.Add(argument);
        using Process process = Process.Start(startInfo) ?? throw new InvalidOperationException($"Could not start '{executable}'.");
        Task<string> outputTask = process.StandardOutput.ReadToEndAsync();
        Task<string> errorTask = process.StandardError.ReadToEndAsync();
        await process.WaitForExitAsync();
        return new ProcessResult(process.ExitCode, (await outputTask) + (await errorTask));
    }

    private static int NormalizeTimeout(int seconds) => Math.Clamp(seconds, 30, 600);
    private static string FormatDevices(IEnumerable<string> values) => string.Join(", ", values);

    private readonly record struct ProcessResult(int ExitCode, string Output);
}
