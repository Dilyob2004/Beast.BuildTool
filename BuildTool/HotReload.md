# Hot reload: Windows и Android-эмулятор

Режим разработки для SDK UI-проектов с `BEAST_UI_APPLICATION(SceneType, {...})`.
Обычные `build`/`run` и Release APK не получают загрузчик обновлений.
Произвольный `BEAST_APPLICATION` со своим render loop пока не поддержан.

## Запуск

### Visual Studio 2026

С версии расширения **1.2.0** стандартный профиль новых проектов — **Windows (Beast)**.
Кнопка запуска/F5 читает `Config/BuildConfig.json` при каждом старте:

```json
"hotReload": true
```

В Debug `true` запускает watch, `false` (также отсутствие свойства) — обычный
EXE с native debugger. Для новых проектов записывается `false`. В EcoSbor и
TextFieldDemo включено `true`. Release всегда запускается обычно. Значение
должно быть JSON boolean, не строкой. После правки JSON достаточно сохранить
файл и запустить заново; генерация solution не требуется. Активную watch-сессию
сначала завершите: изменение конфига не переключает уже работающий процесс.

Build/Ctrl+Shift+B по-прежнему только собирает проект и завершается: бесконечный
watch внутри NMakeBuild заблокировал бы запуск. Режим watch запускается без
отладчика VS; поддержка breakpoints в динамически загруженной DLL не добавлена.
Обычный Windows Local Debugger остаётся доступен и обходит эту настройку.
Для старых проектов после `beast generate` может потребоваться один раз выбрать
**Windows (Beast)** в списке возле зелёной кнопки: сохранённый `.vcxproj.user`
не перезаписывается генератором. В TextFieldDemo выбор обновлён явно.

Расширение **Beast BuildTool Launchers 1.1.0** добавляет в Solution Explorer:
**правая кнопка на проекте приложения → Beast: Hot Reload (Windows, Debug)**.
Не на solution и не на BeastSDK. Обычный Local Windows Debugger не изменён.
Команда запускает отдельную консоль watch, всегда Debug, независимо от выбранной
Release-конфигурации VS. Она не подключает native debugger к BeastHost.
Сохраняйте изменения файлов (Ctrl+S); несохранённый буфер редактора не собирается.
Консоль остаётся открытой после ошибки/завершения для просмотра диагностики.
При работающем watcher для этого проекта, сборке или отладке VS команда недоступна.
Контекстный пункт всегда явно запускает watch независимо от hotReload.

VSIX: `BuildTool.VisualStudio/Beast.AndroidLauncher/bin/Beast.AndroidLauncher.vsix`.
При обновлении расширения закройте и снова откройте Visual Studio. Проекты
EcoSbor/TextFieldDemo перегенерированы; для других при необходимости выполните
`beast generate <project>`. Изменения SDK/Build.cs по-прежнему требуют нового watch.

### Терминал

Из корня репозитория, в отдельных терминалах:

```powershell
.\BuildTool\bin\Beast.BuildTool.exe watch Test/EcoSbor --platform Windows
.\BuildTool\bin\Beast.BuildTool.exe watch Test/EcoSbor --platform Android --avd Medium_Phone_API_36.1
```

Для уже работающего эмулятора: `--device emulator-5554`. Путь проекта можно
опустить, если текущий каталог — корень проекта с `Config/BuildConfig.json`.
`--prepare-only` собирает host без запуска приложения/наблюдения (Android всё
равно определяет ABI выбранного эмулятора). Конфигурация — только Debug/dev.

Сохраняйте `.cpp`, `.h`, `.hpp`, `.inl` и shader sources. Watch опрашивает
исходники раз в 500 ms, ждёт стабилизации и собирает один кандидат за раз.
Изменения во время сборки приводят к отбрасыванию устаревшего кандидата и
следующей сборке. Учитываются все исходники Source и заголовки include paths
модулей; `Intermediate`, `Binaries`, служебные каталоги и directory symlinks
не обходятся. Диагностика компилятора остаётся в терминале.

Ожидаемый результат: `Candidate delivered` → `Host: ... ok`. Это именно
подтверждение загрузки/восстановления сцены, не просто успешная линковка.
Если приложение в фоне, замена ожидает активного кадра; следующие изменения
накапливаются до подтверждения предыдущего кандидата. Закрытие приложения
завершает watch. Ctrl+C прекращает наблюдение; Windows host игнорирует этот
console signal и может быть закрыт отдельно. Пока watch работает, его EXE
заблокирован ОС: остановите watch перед пересборкой самого BuildTool.

## Что остаётся постоянным

- Host, нативное окно/Activity, surface lifecycle и RHI.
- Единственная динамическая библиотека SDK: `BeastRuntime.dll` / `libBeastRuntime.so`.
- C++ runtime и SDK caches — модуль не линкует вторую статическую копию Beast.

Код всех проектных модулей собирается в `BeastModule_<generation>.dll` /
`libBeastModule_<generation>.so`. Это новый файл: загруженный бинарник не
перезаписывается. Windows exports SDK формируются из определённых Beast-функций
через MSVC dumpbin; это dev ABI одного toolchain, не стабильный plugin ABI.

Артефакты: `Binaries/<Platform>/HotReload/<session>/`.
Объекты, rules и копия Gradle: `Intermediate/Build/HotReload/<Platform>/<session>/`.
Завершённые сессии пока не удаляются автоматически; не удаляйте активную сессию.

Windows host получает тот же `.rc` проекта (иконка, версия, manifest), что обычный
EXE, с учётом `windows.dpiAware`. Ресурсы компилируются отдельно в HostResources
в кеше сессии. Раньше host не линковал `.res`; исправлено 28.09.2026. Проверена
сборка TextFieldDemo `watch --prepare-only` и извлечение manifest из BeastHost.exe:
`dpiAwareness=PerMonitorV2, PerMonitor`. SDK дополнительно устанавливает DPI
context потока; отсутствие manifest само по себе не доказывает причину любого
переполнения layout. Визуальное сравнение на проблемном мониторе ещё нужно.
После обновления сборщика нужен новый watch: manifest работающего процесса
не меняется при перезагрузке DLL. Изменение Windows-ресурсов требует перезапуска.

## Проверка и переключение

Loader проверяет версию/размер контракта и fingerprint SDK, сборщика,
конфигурации и toolchain. Затем создаёт новую сцену, восстанавливает её данные,
проверяет layout/paint и просит старую сцену остановить внешнюю работу.
Только после этого, между кадрами, заменяет старую сцену.

Compile/link/load/ABI/обычные C++ initialization exceptions не заменяют
last-good сцену. Это **не sandbox**: access violation, UB, зависший конструктор,
падение драйвера и побочные эффекты статических инициализаторов не могут быть
безопасно отменены внутри одного native-процесса. Конструктор кандидата должен
быть локальным и не запускать необратимые внешние действия.

Изменения SDK, Build.cs, BuildConfig, toolchain или параметров окна/RHI требуют
явного перезапуска `watch`. Автоматического hot restart пока нет; это не
выдаётся за hot reload.

## Состояние и жизненный цикл

По умолчанию пересоздаётся дерево UI. Для нужных данных сцены переопределите:

```cpp
std::string SaveReloadState() const override;
bool RestoreReloadState(const std::string& state, const Beast::Window& window) override;
bool QuiesceForReload() noexcept override;
```

Формат состояния версионируется самим приложением; лимит — 1 MiB. Не копируйте
адреса, vtable или сырую память объектов. `false` из Restore/Quiesce отменяет
замену. До уничтожения старой сцены нужно остановить/join собственные потоки и
снять внешние callbacks. Обработчики внутри её widget tree уничтожаются с ней.
Текущий жест/анимация не переносится автоматически.

В EcoSbor уже добавлены вкладка, раскрытые разделы, настройки и позиции
вертикальной/горизонтальной прокрутки. `ReloadStateTests.cpp` проверяет round trip.
В `Test/HotReloadDemo` показан перенос счётчика и обработчика кнопки.

**Ограничение первой реализации:** код старых модулей остаётся загруженным до
выхода процесса, чтобы отложенные GPU handles/custom deleters не указывали в
выгруженный код. Максимум 32 успешных загрузки библиотек за сессию, включая
initial и библиотеки с отвергнутым контрактом/конструктором. Затем требуется
новая watch-сессия. Сцены уничтожаются, но полное безопасное освобождение
модулей без этого ограничения — отдельный следующий этап.

## Android

Watch создаёт отдельную копию Gradle-проекта и отдельное приложение
`com.beast.<project>.hot`. Обычный APK и его данные не заменяются. Dev APK
подписан; устанавливается один раз при старте сессии. ABI определяется по
эмулятору: x86_64 либо arm64-v8a, без обязательной ARM-трансляции.

Следующие сборки доставляются ADB/run-as в приватную `files/beast-hot`.
Файл открывается, становится read-only до записи, затем проверяется SHA256.
Только после этого атомарно публикуется mailbox request. Нет HTTP-сервера,
открытого сетевого порта или загрузчика обновлений в обычном Release.
Хеш защищает от повреждения передачи, а не от недоверенного разработчика:
используйте только собственный проверенный код.

Ограничения Android DCL и хранение кода учтены по официальным документам:
[Android 14 behavior changes](https://developer.android.com/about/versions/14/behavior-changes-14#safer-dynamic-code-loading),
[Dynamic code loading security](https://developer.android.com/privacy-and-security/risks/dynamic-code-loading).

## Проверено 26.09.2026

- Windows/MSVC/OpenGL, demo: PID 19784 оставался тем же; версии 1→2→4→5→7→8,
  счётчик кадров переносился, compile/link/init/ABI failures сохраняли сцену.
- Android emulator API 36.1/x86_64/OpenGL ES: PID 4663 не менялся, версии 4→5→7→8;
  два нажатия кнопки пережили отказ кандидата и последующее обновление.
  Проверены compile/link/init/ABI rejection, права SO `-r--------`.
- EcoSbor/Vulkan: Windows PID 38780 и Android PID 8362, изменение текста
  заголовка без перезапуска; профиль и прокрутка перенесены. Финальная доставка
  Android выполнялась с SHA256-проверкой. В эмуляторе validation layer отсутствует;
  на Windows Vulkan validation включена.
- Полный EcoSbor UI suite и ReloadStateTests прошли; обычный Windows Debug
  проект собирается. BuildTool: 0 warnings/errors в завершённой сборке.

Не заявлено: физический ARM64 hot reload, бесконечная выгрузка модулей, любые
native crashes без последствий, hot reload произвольного custom Application.
