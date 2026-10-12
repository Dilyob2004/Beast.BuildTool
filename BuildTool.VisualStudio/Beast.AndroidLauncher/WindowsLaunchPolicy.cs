using System;
using System.IO;
using Newtonsoft.Json;
using Newtonsoft.Json.Linq;

namespace Beast.AndroidLauncher;

internal static class WindowsLaunchPolicy
{
    public static bool UseWatch(string configFile, string configuration)
    {
        if (configuration != "Debug" && configuration != "Release")
            throw new InvalidOperationException("Unsupported Windows launch configuration: " + configuration);
        var config = JObject.Parse(File.ReadAllText(configFile), new JsonLoadSettings { DuplicatePropertyNameHandling = DuplicatePropertyNameHandling.Error });
        var value = config.GetValue("hotReload", StringComparison.OrdinalIgnoreCase);
        if (value != null && value.Type != JTokenType.Boolean)
            throw new InvalidOperationException("Config/BuildConfig.json: hotReload must be true or false, not a string.");
        bool watch = value != null && value.Value<bool>() && configuration == "Debug";
        if (watch && config.GetValue("engine", StringComparison.OrdinalIgnoreCase) is JObject engine &&
            engine.GetValue("enabled", StringComparison.OrdinalIgnoreCase)?.Value<bool>() == false)
            throw new InvalidOperationException("Hot reload requires engine.enabled=true and a BEAST_UI_APPLICATION project.");
        return watch;
    }
}
