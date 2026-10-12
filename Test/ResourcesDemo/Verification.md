# Проверка ресурсов RHI — 2026-09-11

## Среда

- Windows, MSVC 14.51.36231, C++20; OpenGL 4.6 core на AMD Radeon Graphics.
- Windows Vulkan: AMD Radeon Graphics, VK_LAYER_KHRONOS_validation включён.
- Android: AVD Medium_Phone_API_36.1, APK arm64-v8a, NDK 29.0.14206865,
  Java Android Studio JBR, emulator `-gpu host`. OpenGL ES 3.1; Vulkan Goldfish
  GFXStream / NVIDIA GeForce RTX 5060 Laptop GPU. Это эмулятор, не физический телефон.
- DXC, SPIRV-Cross, spirv-val из Vulkan SDK 1.4.335.0.

## Результат

Windows и Android Debug собраны своим Beast BuildTool, без предупреждений в
полученных логах сборки. Текущий Launch.cpp и основные EXE/APK выбирают OpenGL.
Проверенные снимки OpenGL/Vulkan лежат в соответствующих подпапках Binaries.
`ResourcesDemo.sln` также успешно собран через Visual Studio MSBuild
Debug|x64: SDK и приложение делегируют сборку Beast, shader cache используется.

- `OpenGLLoaderTests`: прошёл.
- `OpenGLFrameTests`: FBO/fence, таймауты, ошибки выделения, generation/context loss — прошёл.
- `OpenGLFrameGpuTests`: реальный GPU, offscreen RGBA8/размеры/пиксели,
  resize и пересоздание — прошёл. **Оконный readback отдельно пропущен**:
  на этом хосте прямая очистка/readback default framebuffer возвращает неопределённые
  пиксели. Это не заявляется как подтверждённая проверка пикселей Windows-окна.
- `RHITests`: clear-only API, Windows OpenGL/Vulkan, resize/recreation — прошёл.
- `RHIResourceTests`: CPU-валидация, повреждённый SPIR-V, неправильные стадии,
  текстуры, pipeline, порядок команд, копирование вершин и удержание handles — прошёл.
- OpenGL resource GPU test: 40 кадров, **440 проверок пикселей настоящего FBO**:
  RGBA8 texture orientation, straight/premultiplied alpha, scissor, clear/draw order.
  Resize, замена ресурсов, уничтожение контекста при сохранении того же RHI
  (generation change), полное пересоздание RHI — прошли.
- Vulkan resource GPU test: 40 текстурированных кадров, resize, освобождение
  ресурсов после fence и пересоздание устройства — прошёл. Validation callback
  не выдал warning/error. Проверка Vulkan-пикселей выполнялась на Android,
  Windows Vulkan проверялся по выполнению и validation, не readback.
- ShaderCompiler.Tests: кеш-хит не перезаписывает header; изменения include
  и defines меняют результат; ошибочный HLSL/unsupported binding/duplicate name/
  C++ keyword/отсутствующий configured tool останавливают сборку. Повреждённый
  SPIR-V cache и удалённый generated header восстанавливаются. Пути с пробелами
  работают. Все проверки прошли.

Android screenshots: `Intermediate/Verification/android-opengl.png`,
`android-vulkan.png` и варианты `*-resumed.png`, 1080x2400.
Для каждого кадра проверены 11 ожидаемых RGB-значений, включая blending.
Сравнение сетки без status/navigation bars: **0 расхождений из 19 109 точек**,
допуск ±1 на канал. Совпали OpenGL/Vulkan и кадры каждого API до/после resume.

Оба API проверены после длительного Home → resume с фактическим
SurfaceDestroyed → SurfaceCreated: OpenGL PID 6610, Vulkan PID 4607 остались
прежними, новые поверхности снова показали текстуры и pipeline. Логи сохранены
в `Intermediate/Verification/android-opengl.log` и `android-vulkan.log`.
На Android validation layer не установлен — отсутствие его сообщений не
выдаётся за Android validation pass. Проверка настоящего EGL_CONTEXT_LOST
на физическом Android GPU не проводилась; этот путь проверен через generation
с реальной заменой GL-контекста в Windows GPU test.

Команды воспроизведения и ограничения ABI v1 описаны в
`BeastSDK/Source/RHI/Resources.md`. Файлы в Binaries/*/Debug/OpenGL и Vulkan —
ручные снимки, не отдельные конфигурации сборщика; новые сборки их не обновляют.
