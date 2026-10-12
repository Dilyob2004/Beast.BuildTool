# Проверка DemoApp — 2026-09-10

Исходник `Source/Launch/Launch.cpp` собран по очереди с enum OpenGL и Vulkan,
без альтернативной реализации отрисовки в тестовой программе. Обычные EXE/APK
и исходник в конце возвращены к OpenGL; отдельные снимки обоих бэкендов находятся в Binaries.

| Платформа | Бэкенд | Проверка |
| --- | --- | --- |
| Windows x64 Debug | OpenGL | MSVC-сборка; реальный 4.6.0 Core Profile; первый Submit успешен; штатное закрытие, exit 0 |
| Windows x64 Debug | Vulkan | MSVC-сборка; AMD Radeon(TM) Graphics; validation включён, сообщений об ошибках нет; первый Submit успешен; штатное закрытие, exit 0 |
| Android ARM64 Debug | OpenGL ES | NDK 29; ES 3.1, NVIDIA host GPU; установка, запуск, серый кадр, Home/resume |
| Android ARM64 Debug | Vulkan | NDK 29; Goldfish GFXStream / NVIDIA RTX 5060 Laptop GPU; установка, запуск, серый кадр, Home/resume |

Android AVD: Medium_Phone_API_36.1, cold boot без сохранения snapshot,
`-gpu host`. Это эмулятор, не физическое Android-устройство.
Vulkan validation layer на Android не установлен: это явно отмечено в runtime-логе,
а не считается проверкой Android validation.

## Цвет и жизненный цикл

- Оба backend используют один CPU-список `ClearFrameBuffer({0.1f,0.1f,0.1f,1.0f})`.
- На Android сделаны и визуально проверены `Intermediate/Verification/android-opengl.png`
  и `android-vulkan.png`: только однородный серый фон плюс системные панели Android.
- Сравнена сетка из 3200 точек внутри приложения, без системных панелей:
  расхождений между API нет; все точки имеют RGBA `(25,25,25,255)` после
  преобразования к 8-битному изображению. Переданное в RHI значение остаётся `(0.1,0.1,0.1,1)`.
- Vulkan: PID 4698, surface destroyed → created → frame presented после Home/resume.
- OpenGL: PID 4894, тот же цикл и повторный успешный кадр без перезапуска процесса.
- На Windows проверены реальный запуск DemoApp, успешный Submit и штатное закрытие.
  Захват/сравнение отображённых Windows-пикселей в этой проверке не выполнялись.
- Приложение не содержит таймера автоматического закрытия. Тестовые Windows-окна
  закрыты извне штатным сообщением закрытия; временный headless-эмулятор закрыт после проверки.

## Дополнительные проверки

- RHITests: `/W4 /WX`; ClearFrameBuffer записывает закрытый список без прямоугольников,
  сохраняет заданный RGBA, отклоняет неверный цвет и повторную/вложенную запись.
  Существующие реальные OpenGL/Vulkan тесты resize/recreate также прошли.
- Beast.BuildTool.sln: .NET build, 0 warnings / 0 errors.
- DemoApp.sln: сгенерирован и собран через установленный Visual Studio MSBuild,
  Debug x64, с зависимостью BeastSDK. Первому запуску MSBuild в sandbox был недоступен
  каталог Windows SDK; повтор с доступом к установленному SDK прошёл.
- Архивные EXE и APK OpenGL/Vulkan имеют разные SHA-256; это отдельные варианты,
  а не две копии одной сборки. Оба APK используют `com.beast.demoapp` и заменяют друг друга.

Тест не измеряет frame pacing/120 Гц и не проверяет долговременную нагрузку,
физические устройства или принудительную потерю GPU.
