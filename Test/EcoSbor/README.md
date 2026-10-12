# ЭкоСбор на Beast

C++-перенос пользовательского `Prototypes/flutter_application_1`, без Flutter/Dart
и без Java UI. Оригинальный проект не изменён. Четыре вкладки: главная, гид,
карта и профиль. Код экрана — `Source/Launch/EcoScene.cpp`, данные — `EcoData.h`.

Работают вертикальная прокрутка, горизонтальная статистика, раскрытие шести
разделов гида, переключатели настроек, сохранение положения вкладок и состояния
в пределах процесса. Мышь/колесо, Shift+колесо для горизонтальной прокрутки,
касания и Tab/Space/Enter. На Windows Escape закрывает приложение.
Drag/касания теперь имеют упругие края и пружинный возврат, обе оси. Колесо
и программный SetOffset остаются ограниченными. Back возвращает на главную;
Back на главной завершает приложение. Lifecycle RHI, ввод UI и стандартные
клавиши теперь обслуживает `Beast::UI::Application`, а не Launch.cpp.
Launch.cpp содержит только include и `BEAST_UI_APPLICATION` с параметрами окна
и backend. Повторяющиеся builders компоновки находятся в `UI/Build.h` SDK;
Compose использует готовый Scaffold, сохраняя логику вкладок в приложении.

Как и в исходнике, карта и подписка на запуск — заглушки. Тёмная тема меняет
только положение переключателя; уведомления — локальный флаг, без реальных push.
Информационные пункты профиля пока не открывают страницы. На диск состояние
не сохраняется. Данные гида перенесены из прототипа, не проверены по правилам
конкретного города/пункта приёма. Вместо emoji используются доступные иконки.

## Сборка из корня репозитория

Замеры CPU/GPU и пула слоёв: [Performance.md](Performance.md).
Пул работает внутри SDK; включать его в коде приложения не требуется.

```powershell
.\BuildTool\bin\Beast.BuildTool.exe generate Test/EcoSbor
.\BuildTool\bin\Beast.BuildTool.exe build Test/EcoSbor --platform Windows --config Debug
.\BuildTool\bin\Beast.BuildTool.exe build Test/EcoSbor --platform Android --config Debug
.\Test\EcoSbor\Tests\Run-Tests.ps1
```

Открыть `EcoSbor.sln`. Выбор API — поле `.backend` в `Source/Launch/Launch.cpp`,
сейчас OpenGL. Продукты: `Binaries/Windows/Debug/EcoSbor.exe` и
`Binaries/Android/Debug/EcoSbor.apk`. Windows распространяется вместе с
`BeastSDK-LICENSES.txt`; APK содержит уведомления внутри assets.

## Проверено 2026-09-12

Windows build и строгая компиляция новых UI-исходников `/W4 /WX` — PASS.
`Tests/Run-Tests.ps1` — PASS: текст, scroll/tap cancellation, pointer IDs,
инерция и остановка при потере фокуса, колесо, clipping, четыре вкладки,
раскрытие гида, настройки, сохранение offsets, разные viewport/DPI.
Реальные Windows OpenGL/Vulkan: по 25 кадров, переходы, resize и пересоздание RHI.
Vulkan validation включён, ошибок/предупреждений в журнале нет.
Шесть OpenGL кадров просмотрены: `Intermediate/Verification/windows-*.bmp`.
Vulkan pixel readback отдельно не реализован.

Android APK успешно собран с NDK 29.0.14206865. Установка/запуск в этой проверке
не подтверждены: тестовый эмулятор вернул `adb: device unauthorized`, в том числе
после перезапуска adb. Для продолжения откройте эмулятор и разрешите USB debugging
этому компьютеру, затем выполните `beast run Test/EcoSbor --no-build`.
Отсутствие Android runtime-проверки не выдаётся за успешный тест.

Визуально это близкий перенос, не побитовая копия Flutter: Noto Sans вместо
системного Roboto, два начертания вместо всех весов, приближённые мягкие тени,
собственная пружинная физика вместо точной копии Flutter. Списки eager, не виртуализированы.
На узком экране сетка перестраивается в две колонки; карта тоже прокручивается.

## Обновление прокрутки и UI host

`BeastSDK/Tests/UIScrollHostTests.cpp`: обе границы/оси, короткое содержимое,
отмена/regrab, resize/focus loss, траектория пружины при 30/60/120 Hz, отсутствие
повторного layout на скролле, reuse текстур при 75–400% DPI. Проверяется SDK host
с fake RHI (порядок lifecycle, сохранение сцены, ввод и Back/Escape) и настоящий
Windows OpenGL/Vulkan host с анимацией резинки и завершением приложения.
Регрессионные layout/input/DPI тесты SDK также проходят, включая OpenGL readback.

`Tests/ScrollBench.cpp`: EcoSbor Debug, 1080×1800, scale 2.625, 32 шага по 3.7 dp.
До оптимизации CPU-подготовка кадра: медиана 46.292 мс, p95 55.439 мс;
после: 4.673 мс / 7.963 мс. Новые относительно предыдущего кадра текстуры:
949 → 6 за 31 переход (включая вновь появляющиеся строки). Это BuildFrame без
GPU Submit/Present, не показатель FPS телефона. Логи — `Intermediate/Verification`.
Убраны полный layout на каждом смещении и повторная растеризация видимых строк.

EXE и APK пересобраны. Для этого обновления Android runtime пока не проверен:
`adb devices -l` вернул пустой список, эмулятор/телефон не подключён. Предыдущая
ошибка авторизации описана выше; успешная APK-сборка не заменяет runtime-тест.
