# DemoApp

Минимальный пример Beast: окно/Android NativeActivity очищается цветом
`(0.1f, 0.1f, 0.1f, 1.0f)`. Без текста, прямоугольников и шейдеров.
Приложение остаётся открытым до закрытия пользователем, Escape на Windows
или завершения Android Activity. Автоматического выхода после нескольких кадров нет.

## Выбор бэкенда

В `Source/Launch/Launch.cpp` измените одну строку:

```cpp
constexpr Beast::RHIBackend Backend = Beast::RHIBackend::OpenGL;
// Либо Beast::RHIBackend::Vulkan
```

Затем пересоберите проект. Windows OpenGL требует 4.6 Core; на Android этот же
enum выбирает OpenGL ES. Неявного fallback между API нет.

В OnStart записывается `commands_.ClearFrameBuffer(Background)`, на каждом кадре
вызывается Submit. Постоянный закрытый список используется повторно; после
потери Android surface новый RHI продолжает использовать эту CPU-запись.
Изменение цвета в ходе выполнения требует Reset и повторной записи списка.

## Сборка и запуск

Откройте `DemoApp.sln` в Visual Studio или выполните из корня Beast.BuildTool:

```powershell
.\BuildTool\bin\Beast.BuildTool.exe build Test/DemoApp --platform Windows --config Debug
.\Test\DemoApp\Binaries\Windows\Debug\DemoApp.exe

.\BuildTool\bin\Beast.BuildTool.exe build Test/DemoApp --platform Android --config Debug
.\BuildTool\bin\Beast.BuildTool.exe run Test/DemoApp --no-build --wait
```

SDK/NDK/Java и AVD меняются в `Config/BuildConfig.json`.
Для этого примера выбран существующий AVD `Medium_Phone_API_36.1`.

## Готовые продукты

Обычная сборка обновляет `Binaries/Windows/Debug/DemoApp.exe` и
`Binaries/Android/Debug/DemoApp.apk` согласно выбранному enum.
По умолчанию в исходнике и обычных продуктах оставлен OpenGL.

Снимки обеих проверенных сборок дополнительно сохранены:

| Бэкенд | Windows | Android |
| --- | --- | --- |
| OpenGL | `Binaries/Windows/Debug/OpenGL/DemoApp.exe` | `Binaries/Android/Debug/OpenGL/DemoApp.apk` |
| Vulkan | `Binaries/Windows/Debug/Vulkan/DemoApp.exe` | `Binaries/Android/Debug/Vulkan/DemoApp.apk` |

Эти подпапки — снимки проверки, обычная команда build не обновляет их автоматически.
Оба APK имеют package `com.beast.demoapp`: установка одного заменяет другой.
Для установки сохранённого варианта используйте `adb install -r <путь-к-APK>`
и `adb shell am start -n com.beast.demoapp/.GameActivity`.

Проверки и ограничения описаны в `Verification.md`.
