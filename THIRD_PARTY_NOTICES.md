# Third-party notices

The root MIT license applies to original Beast code. It does not replace the
licenses or copyright notices of third-party code, fonts, generated data, or tools.
Keep the relevant notices when redistributing source or binaries.

| Component | License / notice location |
| --- | --- |
| HarfBuzz | [Old MIT and per-file exceptions](BeastSDK/ThirdParty/HarfBuzz/LICENSE); also `src/ms-use/COPYING` |
| SheenBidi | [Apache-2.0](BeastSDK/ThirdParty/SheenBidi/LICENSE) |
| Noto Sans | [SIL Open Font License 1.1](BeastSDK/ThirdParty/NotoSans/LICENSE) |
| Noto fallback fonts | [SIL Open Font License 1.1](BeastSDK/ThirdParty/NotoFallback/LICENSE) |
| Material Icons | [Apache-2.0](BeastSDK/ThirdParty/MaterialIcons/LICENSE) |
| Unicode data | [Unicode license](BeastSDK/ThirdParty/Unicode/LICENSE) |
| Vulkan-Headers | [License overview and individual notices](BeastSDK/ThirdParty/Vulkan-Headers/LICENSE.md) |
| OpenGL / EGL / Khronos headers | Notices in each header under `BeastSDK/ThirdParty/OpenGL` |
| stb / imstb_truetype | License and attribution embedded in the files under `BeastSDK/ThirdParty/stb` |
| Gradle wrapper | [Apache-2.0](BuildTool/Templates/Android/Project/gradle/wrapper/LICENSE); Gradle build tool wrapper, redistributed with the project templates |

NuGet, Gradle/Maven, Android SDK/NDK, Java, Visual Studio, and shader compiler
dependencies are separately licensed. Installing or distributing them may have
additional requirements; they are not relicensed by this repository.

Generated font data remains subject to the corresponding font license. Android
builds include SDK font/library notices in their assets. Local research material
under `Prototypes/` and obsolete prebuilt template libraries are not distributed
as part of this repository.
