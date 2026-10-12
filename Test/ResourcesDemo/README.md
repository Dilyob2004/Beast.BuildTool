# ResourcesDemo

Один HLSL, две текстуры, три graphics pipeline; Windows и Android.
Открыть `ResourcesDemo.sln`. Выбор OpenGL/Vulkan — enum `Backend` в
`Source/Launch/Launch.cpp`, затем пересобрать нужную платформу.
Шейдеры объявлены в `Source/Launch/Launch.Build.cs`; рисунок — в `ResourceScene.h`.
`DemoApp` не изменён и остаётся примером ClearFrameBuffer.

На экране: четыре цветных сектора текстуры, magenta-патч (порядок ClearRect после
draw), светлая полоса с straight alpha, нижняя полоса с premultiplied alpha и
cyan-патч, ограниченный scissor. Фон (0.1, 0.1, 0.1, 1). Статус/навигация Android сохранены.

Из корня репозитория:

```powershell
./BuildTool/bin/Beast.BuildTool.exe generate Test/ResourcesDemo
./BuildTool/bin/Beast.BuildTool.exe build Test/ResourcesDemo --platform Windows --config Debug
./BuildTool/bin/Beast.BuildTool.exe build Test/ResourcesDemo --platform Android --config Debug
./BuildTool/bin/Beast.BuildTool.exe run Test/ResourcesDemo --no-build --wait
```

Текущие продукты: `Binaries/Windows/Debug/ResourcesDemo.exe` и
`Binaries/Android/Debug/ResourcesDemo.apk`. Подпапки `OpenGL`/`Vulkan` содержат
снимки проверенных вариантов, **не обновляются автоматически** последующими сборками.
Оба APK используют пакет `com.beast.resourcesdemo`, установка другого заменяет приложение.

Конфиг содержит пути к установленным shader tools, Java, SDK/NDK и имя AVD.
На другом компьютере измените `Config/BuildConfig.json`.
DXC/SPIRV-Cross нужны только при сборке; шейдеры встроены в EXE/APK.
Подробный API/ограничения: `BeastSDK/Source/RHI/Resources.md` в репозитории.
