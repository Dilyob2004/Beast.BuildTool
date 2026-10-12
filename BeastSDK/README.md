# Beast — platform, RHI и UI

Одна C++20-библиотека движка: `Beast.lib` (Windows/MSVC) и `libBeast.a`
(Android/Clang, ARM64). Внутри SDK нет модулей `*.Build.cs`.
Папки `Source/Core`, `Source/Input`, `Source/Platform` — организация исходников,
а не отдельные библиотеки. Публичная точка включения: `#include "Beast.h"`.

## Что реализовано

- `Application`: Configure, OnStart, OnEvent, OnUpdate, OnStop и RequestExit.
- `Window`: заимствованный HWND/ANativeWindow, размер поверхности в физических
  пикселях, масштаб DPI, область содержимого, видимость и фокус.
- `Input`: held/pressed/released, клавиатура, мышь, колесо и касания с устойчивыми ID.
  Нажатие и отпускание за один кадр не теряются; потеря фокуса сбрасывает ввод.
- Windows: нативное окно, resize, минимальные размеры, DPI, Unicode, event loop.
- Android: NativeActivity + NDK native_app_glue, lifecycle, pause/resume,
  пересоздание поверхности, ввод. В фоне цикл блокируется на событиях.
- RHI: OpenGL/WGL, OpenGL ES/EGL и Vulkan-Hpp; запись RHICommandList,
  очистка фона и прямоугольников, HLSL shaders, RGBA8 textures, graphics pipeline,
  DrawTriangles, alpha blending, resize и presentation. Подробности в `Source/RHI/README.md`
  и `Source/RHI/Resources.md`; пример — `Test/ResourcesDemo`.
- Beast UI: общее C++-дерево `Widget`, `Text`, `Button`, `Row`, `Column`, `Flex`,
  `Expanded`, `Container`, `Padding`, `Center`; layout в логических единицах,
  пропорциональные размеры, выравнивания, тема, clipping, собственный текст и
  сглаженные скругления через RHI. Мышь, touch, Tab/Shift+Tab, Space/Enter,
  hover/pressed/disabled/focus. API и ограничения — `Source/UI/README.md`;
  работающий пример — `Test/ButtonDemo`.

Все callbacks и обращения к Application выполняются на потоке приложения
(Windows — оконный поток, Android — native_app_glue thread, не Java UI thread).
RequestExit и состояние ввода пока не являются потокобезопасными.
OnStart не гарантирует наличие поверхности: ресурсы вывода нужно создавать в
SurfaceCreated и освобождать в SurfaceDestroyed. Внутри SurfaceDestroyed
нативный handle ещё действителен; после callback хранить его нельзя.
OnStop завершает экземпляр Application, а не каждую потерю фокуса.
На Android размеры/заголовок окна определяет ОС; WindowDefinition задаёт
размеры прежде всего для Windows. Android insets доступны в content* полях.

## Подключение и пример

`beast new` создаёт Launch.cpp из `BuildTool/Templates/Launch.cpp` с подклассом
Application и макросом `BEAST_APPLICATION(MyApplication)`.
Пример расположен в `Test/EcoSbor`.

Из корня репозитория:

```powershell
.\BuildTool\bin\Beast.BuildTool.exe new MyApp --path Test
.\BuildTool\bin\Beast.BuildTool.exe build Test/MyApp --platform Windows --config Debug
.\BuildTool\bin\Beast.BuildTool.exe build Test/MyApp --platform Android --config Debug
.\BuildTool\bin\Beast.BuildTool.exe run Test/MyApp --no-build --avd Medium_Phone_API_36.1 --wait
```

Сборщик сам подключает Source SDK, собирает одну статическую библиотеку и
линкует её с Launch и остальными модулями приложения. `engine.sdkRoot` в
Config/BuildConfig.json позволяет выбрать SDK; пустое значение использует
SDK рядом со сборщиком. `engine.enabled: false` отключает интеграцию для
самостоятельных приложений со своей точкой входа.

Генератор .sln добавляет проект BeastSDK, ссылающийся на оригинальные исходники,
фильтры папок и зависимость приложения. Компиляцией управляет Beast BuildTool,
а не встроенные C++ targets Visual Studio. Команда `beast sdk <project>`
собирает только библиотеку. Кеш библиотеки расположен в
`Intermediate/Build/BeastSDK/<platform>/<configuration>`, продукты приложения —
в `Binaries/<platform>/<configuration>`.

## Проверки

### Память

`Core/Memory/Memory.h` (также включён в `Beast.h`) предоставляет
`Beast::Memory::Allocate(size, alignment)` и `Beast::Memory::Free(pointer)`.
По умолчанию alignment равен `alignof(std::max_align_t)`. Явное выравнивание
должно быть ненулевой степенью двойки; поддерживаются значения 1, 2, 4 и выше.
Размер не обязан быть кратным выравниванию. Нулевой размер, неверные параметры,
переполнение или ошибка выделения возвращают `nullptr`; `Free(nullptr)` допустим.

```cpp
void* memory = Beast::Memory::Allocate(1024, 64);
if (memory) {
    // Работа с сырым блоком памяти.
    Beast::Memory::Free(memory);
}
```

Это не создание C++-объектов: конструкторы и деструкторы не вызываются.
Освобождать блок нужно только через Memory::Free, не через delete/std::free.
Windows использует `_aligned_malloc/_aligned_free`, Android — `posix_memalign/free`.
Слой не перенаправляет глобальные new/delete или аллокаторы STL; пулы, статистика
и GPU-память пока не добавлены. Независимые блоки можно выделять из разных потоков.
`Tests/MemoryTests.cpp` — отдельный тест выравнивания, некратных размеров,
неверных параметров, переполнения, записи в блок и многопоточного выделения.

После Debug-сборки EcoSbor, из x64 Developer Command Prompt в корне репозитория:

```bat
cl /nologo /std:c++20 /EHsc /MDd /W4 /WX /I BeastSDK\Source BeastSDK\Tests\MemoryTests.cpp /FoTest\EcoSbor\Intermediate\MemoryTests.obj /FeTest\EcoSbor\Intermediate\MemoryTests.exe Test\EcoSbor\Intermediate\Build\BeastSDK\Windows\Debug\Beast.lib
Test\EcoSbor\Intermediate\MemoryTests.exe
```

### Приложение и ввод

`Tests/CoreTests.cpp` проверяет переходы ввода, touch IDs, потерю фокуса,
реальное Windows-окно, доставку событий, завершение и обработку исключений.
После сборки EcoSbor Debug, из Developer Command Prompt (x64), в корне репозитория:

```bat
cl /nologo /std:c++20 /EHsc /MDd /W4 /WX /I BeastSDK\Source BeastSDK\Tests\CoreTests.cpp /FoTest\EcoSbor\Intermediate\CoreTests.obj /FeTest\EcoSbor\Intermediate\CoreTests.exe Test\EcoSbor\Intermediate\Build\BeastSDK\Windows\Debug\Beast.lib user32.lib gdi32.lib
Test\EcoSbor\Intermediate\CoreTests.exe
```

## Границы этапа

Это основа платформы и первый этап собственного UI, не полный аналог Flutter.
Пока поддерживается одно окно; iOS/Linux/macOS не реализованы. ScrollView,
TextField, shaping, RTL, fallback и базовые accessibility-мосты уже добавлены,
но остаются ограничения IME, текстовой компоновки и платформенной проверки.
Актуальные детали: [TextFeatures](Source/UI/TextFeatures.md).
Одинаковая логика и отрисовка используются на Windows/Android; размеры в пикселях
зависят от DPI и доступной области. Системные панели принадлежат ОС.
Без gpuRendering Windows показывает диагностический текст через GDI, Android —
одноцветный CPU-буфер. Новый шаблон использует RHI и GPU presentation;
примеры для обоих API находятся в `Test/DemoApp` и `Test/ResourcesDemo`.

Из `Prototypes/Source` использованы архитектурные идеи окна и разделения
платформы. Старые NVRHI/scene/game-DLL зависимости не перенесены.
Android использует glue из выбранного установленного NDK, а не копию его исходников.

UI использует встроенный Noto Sans Regular (SIL OFL 1.1) и stb_truetype
(MIT, вариант Dear ImGui). Beast BuildTool помещает уведомления в
`Binaries/<platform>/<configuration>/BeastSDK-LICENSES.txt`, а на Android —
также в assets APK. Сохраняйте этот файл при распространении Windows-приложения.
Проверка UI, компоновки и DPI после сборки ButtonDemo Windows Debug:
`./BeastSDK/Tests/Run-UITests.ps1` из корня репозитория.
