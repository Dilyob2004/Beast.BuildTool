# Beast — нативный UI на C++

Beast объединяет C++20 SDK интерфейсов и инструмент сборки на C# для Windows и
Android 10+. Рендеринг работает через OpenGL / OpenGL ES или Vulkan; правила
модулей описываются в `*.Build.cs`. Это экспериментальный проект: API меняется,
производительность, ввод текста и accessibility ещё требуют развития.

Это не среда исполнения Flutter/Dart и не библиотека Boost.Beast.

Полный [README с примерами и скриншотами](../README.md) доступен на английском,
чтобы проект был понятен международному сообществу.

## Быстрый старт

Нужны Windows, .NET SDK с поддержкой .NET 8, Visual Studio с компонентами C++ и
Windows SDK. В PowerShell из корня репозитория:

```powershell
dotnet build BuildTool/Beast.BuildTool.csproj -c Release
Copy-Item Test/EcoSbor/Config/BuildConfig.example.json Test/EcoSbor/Config/BuildConfig.json
.\BuildTool\bin\Beast.BuildTool.exe build Test/EcoSbor --platform Windows --config Debug
.\Test\EcoSbor\Binaries\Windows\Debug\EcoSbor.exe
```

Копируйте пример конфига только при первой настройке. В нём можно указать свои
пути к Java, Android SDK и NDK. Локальные конфиги и ключи подписи исключены из Git.

- [SDK](../BeastSDK/README.md)
- [Hot reload](../BuildTool/HotReload.md)
- [Подпись Android APK](../BuildTool/AndroidRelease.md)
- [Текст и ограничения](../BeastSDK/Source/UI/TextFeatures.md)
- [План развития](../ROADMAP.md)
- [Как помочь проекту](../CONTRIBUTING.md)

Исходный код Beast распространяется под MIT. Сторонние компоненты сохраняют
собственные лицензии. Любой разработчик может сделать fork и предложить Pull
Request; решение о включении изменений принимает автор/maintainer.
