<p align="center">
  <img src="BuildTool/Templates/Windows/Resources/Default.png" width="88" alt="Beast logo">
</p>

<h1 align="center">Beast</h1>
<p align="center"><strong>Build native interfaces. Stay in C++.</strong></p>
<p align="center">A C++20 UI SDK and build toolchain for Windows and Android.</p>
<p align="center">
  <a href="LICENSE">MIT</a> · <a href="#quick-start">Quick start</a> ·
  <a href="#see-it-in-action">Demo</a> · <a href="ROADMAP.md">Roadmap</a> ·
  <a href="CONTRIBUTING.md">Contribute</a> · <a href="docs/README.ru.md">Русский</a>
</p>

Beast brings together a retained-mode C++ UI, OpenGL/Vulkan rendering, and a
build tool that turns C# module rules into native Windows executables and Android
packages. Write your application in C++, iterate with development hot reload,
and keep platform lifecycle plumbing inside the SDK.

**Early-stage / experimental.** APIs are evolving. Performance, text input, and
accessibility still need work across devices. Beast is not a Flutter runtime and
does not run Dart applications. It is an independent project, not part of Boost.Beast.

## See it in action

<p align="center">
  <img src="docs/images/ecosbor-home.png" width="30%" alt="EcoSbor home screen rendered by Beast">
  <img src="docs/images/ecosbor-guide.png" width="30%" alt="EcoSbor recycling guide rendered by Beast">
  <img src="docs/images/ecosbor-profile.png" width="30%" alt="EcoSbor profile rendered by Beast">
</p>

Actual **Windows/OpenGL captures** of [EcoSbor](Test/EcoSbor), the included C++
sample. They are not design mockups or Flutter screenshots. The same application
source also targets Android; platform behavior and performance can differ.

## What is inside?

- **C++ UI:** layout containers, buttons, text fields, scrolling, rounded clipping,
  shadows, ripple feedback, themes, and repaint/layer caches.
- **Native rendering:** an RHI abstraction over OpenGL / OpenGL ES and Vulkan,
  with shaders, textures, pipelines, command lists, and synchronization.
- **Text foundations:** HarfBuzz shaping, bidirectional text support, font
  fallback, Unicode grapheme/word handling, and a bounded glyph atlas.
- **Build rules in C#:** `*.Build.cs` modules, dependency resolution, SDK linkage,
  Windows resources, and Android packaging/signing.
- **Development workflow:** Visual Studio makefile solutions with file filters,
  Android install/run support, and hot reload for SDK UI applications.

| Today | Implementation |
| --- | --- |
| Development host | Windows; MSVC and .NET build tooling |
| Windows target | Native x64 executable; OpenGL 4.6 or Vulkan |
| Android target | Android 10+ (API 29); NDK Clang, NativeActivity, OpenGL ES or Vulkan |
| SDK library | `Beast.lib` on Windows; `libBeast.a` on Android |
| Linux / macOS / iOS | Not supported yet |

## Quick start

Run the commands below in **PowerShell**, from the repository root.

### 1. Prepare the Windows toolchain

- Git and the **.NET 8 SDK** (or a newer SDK capable of targeting .NET 8).
- Visual Studio with **Desktop development with C++**, an MSVC x64 toolset,
  and Windows SDK. The current local development setup uses Visual Studio 2026.
- A GPU/driver supporting the selected rendering backend.

```powershell
git clone https://github.com/Dilyob2004/Beast.BuildTool.git
cd Beast.BuildTool
dotnet build BuildTool/Beast.BuildTool.csproj -c Release
```

### 2. Run EcoSbor

```powershell
Copy-Item Test/EcoSbor/Config/BuildConfig.example.json Test/EcoSbor/Config/BuildConfig.json
.\BuildTool\bin\Beast.BuildTool.exe build Test/EcoSbor --platform Windows --config Debug
.\Test\EcoSbor\Binaries\Windows\Debug\EcoSbor.exe
```

Copy the example config only on first setup; do not overwrite your existing local
configuration. Sample configs with machine-specific paths are ignored by Git.

### 3. Create your own project

```powershell
.\BuildTool\bin\Beast.BuildTool.exe new MyApp --path Test
.\BuildTool\bin\Beast.BuildTool.exe build Test/MyApp --platform Windows --config Debug
```

The generator creates `Source/Launch`, module rules, `Config/BuildConfig.json`,
platform resources, and a Visual Studio solution. Outputs go under `Binaries`;
build caches go under `Intermediate/Build`.

For a minimal UI, replace `Source/Launch/Launch.cpp` with:

```cpp
#include "Beast.h"

class HelloScene final : public Beast::UI::Scene
{
public:
    HelloScene()
    {
        using namespace Beast::UI;
        auto message = std::make_shared<Text>("Hello, Beast!", 24.0f);
        auto button = std::make_shared<Button>("Say hello", [message] {
            message->SetText("Native UI, written in C++.");
        });
        using namespace Beast::UI::Build;
        ui.SetRoot(Make<Center>(VBox({message, button}, 16)));
    }
};

BEAST_UI_APPLICATION(HelloScene, {.title = "Hello Beast"})
```

Add a module and regenerate IDE files:

```powershell
.\BuildTool\bin\Beast.BuildTool.exe module Core Test/MyApp
.\BuildTool\bin\Beast.BuildTool.exe generate Test/MyApp
```

Modules live under `Source/<Name>/<Name>.Build.cs`. Declare dependencies in your
module rules as needed. `Source` is an include root, so a header can be included
as `#include "Core/Public/Core.h"`.

### Android

Install Android SDK platform 35, build tools, an NDK (the development setup uses
28/29), and Java 17 or a compatible Android Studio JBR. Set `android.javaHome`,
`android.sdkRoot`, and `android.ndkRoot` in the project's `Config/BuildConfig.json`.
Defaults use the usual Windows Android Studio/SDK locations.

```powershell
.\BuildTool\bin\Beast.BuildTool.exe build Test/EcoSbor --platform Android --config Debug
.\BuildTool\bin\Beast.BuildTool.exe run Test/EcoSbor --no-build --wait
```

Connect and authorize a USB device, or use an Android emulator. Use `--device`
or `--avd` to select a target. Standard Android builds target ARM64; the hot-reload
emulator workflow also supports x86_64. Release APKs are signed, but the local
debug-key fallback is **not a production signing identity**. See
[Android signing](BuildTool/AndroidRelease.md) before distribution.

### Hot reload

```powershell
.\BuildTool\bin\Beast.BuildTool.exe watch Test/EcoSbor --platform Windows
# In a separate session, for an already-running emulator:
.\BuildTool\bin\Beast.BuildTool.exe watch Test/EcoSbor --platform Android --device emulator-5554
```

Save application code to trigger a rebuild. This is a development feature for
`BEAST_UI_APPLICATION` projects, not arbitrary C++ process patching. State
preservation uses explicit scene hooks; SDK and build-rule changes can require
a restart. See [hot reload and Visual Studio integration](BuildTool/HotReload.md).

## Explore the source

| Directory | Purpose |
| --- | --- |
| [BeastSDK/Source](BeastSDK/Source) | Core, platform/window/input, RHI, and UI |
| [BuildTool](BuildTool) | C# build orchestration, project generation, templates |
| [BuildTool.VisualStudio](BuildTool.VisualStudio) | Optional Visual Studio launcher extension |
| [Test](Test) | EcoSbor, TextFieldDemo, rendering and hot-reload examples |
| [BeastSDK/Tests](BeastSDK/Tests) / [Tests](Tests) | SDK and build-tool regression tests |

More detail: [SDK](BeastSDK/README.md), [RHI](BeastSDK/Source/RHI/README.md),
[text features and limitations](BeastSDK/Source/UI/TextFeatures.md),
[roadmap](ROADMAP.md). Some detailed development notes are currently in Russian.

## Help shape Beast

Good contributions include minimal bug reproductions, Android device testing,
frame-time investigations, text/IME fixes, accessibility testing, documentation,
and small UI examples. **Issues and pull requests are welcome in English or Russian.**

Start with [CONTRIBUTING.md](CONTRIBUTING.md). Include the platform, GPU/backend,
build configuration, and reproduction steps when reporting a rendering problem.
If the project interests you, a GitHub star helps other developers discover it.

## License

Original Beast code is available under the [MIT License](LICENSE). Third-party
libraries, fonts, headers, and tools retain their own licenses; see
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
