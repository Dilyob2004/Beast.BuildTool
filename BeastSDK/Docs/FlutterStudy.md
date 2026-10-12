# Beast UI: ориентир Flutter и план развития

Дата: 2026-09-13. Это исследование и план, не отчёт о внедрении перечисленных возможностей.

Обновление после исследования: исправлены hard-scissor стыки, добавлены
аналитические rounded rect/border и ripple. Актуальные детали/ограничения —
[UI README](../Source/UI/README.md). Описание прежней диагностики ниже оставлено
как исходная точка; glyph atlas, offscreen layers и Choreographer ещё не внедрены.

## Зафиксированная цель

По уточнению пользователя: собственные **C++-приложения с UI уровня Flutter**.
Запуск Dart/Flutter-приложений без переписывания, совместимость с pub-пакетами,
Dart VM и воспроизведение всего Flutter API не требуются.

Сохраняем Beast BuildTool, C++ API, модули приложений и одну библиотеку Beast.
В SDK остаются UI, рендеринг, платформенный lifecycle и ввод; Launch описывает
приложение/сцену. Первые целевые платформы — Windows и Android. iOS потребует
отдельных toolchain, platform adapter и GPU backend; наличие исходников Flutter
само по себе не добавляет поддержку iOS в Beast.

## Что изучено и что находится в установленном SDK

Фактический корень: `D:/Program Files/FlutterSDK/flutter`.
[Метаданные SDK](<D:/Program Files/FlutterSDK/flutter/bin/cache/flutter.version.json>)
указывают Flutter **3.41.7 stable**, Dart 3.11.5, framework revision
`cc0734ac716fbb8b90f3f9db8020958b1553afa7`, engine revision
`59aa584fdf100e6c78c785d8a5b565d1de4b48ab`.

Просмотрены ключевые участки framework/rendering, DisplayList, Impeller,
типографики, Android/Windows integration и соответствующие ограничения Beast.
Это не полный аудит всех исходников Flutter.

В SDK есть исходники Engine и готовые Android/Windows артефакты. Однако это
не готовый автономный checkout для пересборки Engine: например, директории
Skia/HarfBuzz/ICU, на которые ссылается `txt/BUILD.gn`, в ожидаемых местах этой
установки не обнаружены. Понадобятся зависимости и отдельная процедура сборки.
Ничего в установленном Flutter/Android SDK при исследовании не изменено.

Текущая [онлайн-документация Flutter](https://docs.flutter.dev/perf/impeller)
описывает уже 3.47.2. Её утверждения о доступности backend по умолчанию нельзя
автоматически переносить на локальную 3.41.7. Важнее изученные механизмы,
а не предположение, что конкретный backend одинаков на всех версиях и ОС.

## Почему картинка выглядит чёткой

Это не «всё векторное» и не отсутствие растровых текстур. Примитивы и контуры
задаются геометрически, но вывод всё равно растеризуется в физические пиксели.
Текст в рассмотренном Impeller использует alpha/color glyph atlases; отдельный
шейдер выбирает покрытие или цвет глифа. Поэтому перенос всех букв на SDF сам
по себе не решит типографику, а отключение фильтрации всех текстур не даст
одинаково хорошую картинку при произвольном увеличении.

Качество складывается из нескольких согласованных частей:

- Единая система координат, реальные размеры drawable и DPI. Отдельная
  политика для дробного положения, тонких линий, изображений и текста.
- Сглаживание краёв: у Impeller есть multisample rendering и специальные
  геометрические/шейдерные пути. В просмотренном clip stack прямо учитывается
  4x MSAA. Современная [документация AA](https://docs.flutter.dev/perf/antialiasing)
  также описывает MSAA и SDF, но SDF не является универсальным путём для всего UI.
- Корректное покрытие на общих границах, clip stack и композиция прозрачности.
  Два независимо сглаженных соприкасающихся края могут давать шов даже при
  верных координатах. Это нельзя исправлять только увеличением числа samples.
- Premultiplied alpha, последовательный контракт цвета и фильтрации. Для
  групповой прозрачности и некоторых эффектов необходим промежуточный слой,
  а не умножение alpha каждого потомка по отдельности.
- Типографика: shaping, метрики, переносы, выбор/подмена шрифта, нужный размер
  растеризации и переиспользование глифов. Для визуального сравнения нужно
  зафиксировать одинаковые файлы шрифтов, веса и line height.

Flutter тоже различает быстрый hard clip, AA clip и более дорогую изоляцию
через saveLayer: документация в `painting.dart` прямо описывает bleeding при
некоторых AA-композициях. Не нужно создавать offscreen texture для каждого
widget — это изменит стоимость кадра и расход памяти.

## Подтверждённая проблема Beast: дробный scissor

В [Canvas.cpp](D:/Github/Beast.BuildTool/BeastSDK/Source/UI/Private/Canvas.cpp:81)
левая/верхняя границы scissor округляются через ceil, правая/нижняя — через floor.
Widget также автоматически накладывает clip по своим bounds.

[ClipSeamProbe.cpp](D:/Github/Beast.BuildTool/BeastSDK/Tests/ClipSeamProbe.cpp)
записывает две соседние фигуры с общей границей `x = 10.25 dp` и проверяет
полученные команды. При scale=1 первая получает scissor `[0,10)`, вторая
`[11,40)`: столбец пикселей x=10 отбрасывается обеими. При scale 1.25, 1.5
и 2.625 также получен разрыв в один физический пиксель.

Результат: [ClipSeamProbe.log](D:/Github/Beast.BuildTool/Test/EcoSbor/Intermediate/Verification/ClipSeamProbe.log).
Подтверждена проблема записанных scissor, **не выполнен GPU readback этого
примера и не доказано происхождение каждого возможного шва в EcoSbor**.
В рамках исследования production Canvas не изменён.

Следующая реализация должна разделить грубый scissor/culling и точное clip
coverage. Простая замена ceil/floor на противоположные значения может убрать
дырку, но допустить рисование за границей. Нужны тесты на утечки, вложенные
clip, AA и стыки, а не только проверка одного исчезнувшего зазора.

## Основные разрывы между текущим Beast и целевым UI

1. **Canvas и RHI.** Сейчас команда содержит один цветовой pass и треугольники
   с одной текстурой/scissor. Нет общего API offscreen passes, stencil clips,
   multisample attachments/resolve и изолированной групповой композиции.
   Vulkan pipeline/attachments используют e1; OpenGL FBO — single-sample.
   Наличие FBO и Fence ещё не означает наличие полного 2D-композитора.
2. **Фигуры.** Скругления строятся с фиксированными 12 шагами на угол и AA fringe.
   Нет общего Path со stroke, joins/caps, fill rules и адаптивной точностью
   кривых в физических пикселях. Тени пока приближённые, не полноценный blur.
3. **Текст.** Есть Noto Regular/Bold, простая UTF-8 обработка и stb rasterizer.
   Кэшируются строки, не общие glyph runs/atlases. Нет полноценного shaping,
   bidi, font fallback, emoji fallback, выбора текста и модели редактирования.
4. **Перерисовка.** Последняя оптимизация убрала полный layout на каждом scroll,
   но при изменении кадра по-прежнему записывается общий UI command list.
   Нужны границы локальной перерисовки и отдельные display lists поддеревьев.
   Большие списки пока создают все элементы, а не только видимую область.
5. **Платформенный UI.** Есть окно, safe area, lifecycle, hardware input и базовая
   навигация. Это не заменяет IME/composition, clipboard, accessibility tree,
   экранную клавиатуру, семантический focus и интеграцию native views.
6. **Планирование кадров.** Android loop использует `ALooper_pollOnce` с timeout
   16 мс в активном состоянии. Это не дисплейный frame scheduler. Возможное
   влияние ожидания и Present на плавность надо измерить; оно ещё не измерено.

## Что полезно взять как образец

- [DisplayListBuilder](<D:/Program Files/FlutterSDK/flutter/engine/src/flutter/display_list/dl_builder.h>):
  backend-independent запись Paint/Path/transform/clip/layers. В Beast это
  отдельный 2D display list над RHI, а не замена RHICommandList Flutter-классом.
- [Clip stack](<D:/Program Files/FlutterSDK/flutter/engine/src/flutter/impeller/entity/entity_pass_clip_stack.cc:150>)
  и [clip drawing](<D:/Program Files/FlutterSDK/flutter/engine/src/flutter/impeller/entity/contents/clip_contents.cc>):
  различие простого scissor и геометрического clip с depth/stencil.
- [Path tessellator](<D:/Program Files/FlutterSDK/flutter/engine/src/flutter/impeller/tessellator/path_tessellator.h>)
  и [Wang's formula](<D:/Program Files/FlutterSDK/flutter/engine/src/flutter/impeller/geometry/wangs_formula.h>):
  точность кривой зависит от преобразования, а не фиксированного количества шагов.
- [Paragraph builder](<D:/Program Files/FlutterSDK/flutter/engine/src/flutter/txt/src/skia/paragraph_builder_skia.cc:43>),
  [lazy glyph atlas](<D:/Program Files/FlutterSDK/flutter/engine/src/flutter/impeller/typographer/lazy_glyph_atlas.h>)
  и [Skia typographer](<D:/Program Files/FlutterSDK/flutter/engine/src/flutter/impeller/typographer/backends/skia/typographer_context_skia.cc>):
  разделение layout текста, растеризации и GPU-кэша; padding атласа, alpha/color glyphs.
- [RenderRepaintBoundary](<D:/Program Files/FlutterSDK/flutter/packages/flutter/lib/src/rendering/proxy_box.dart:3443>)
  и [RenderSliverList](<D:/Program Files/FlutterSDK/flutter/packages/flutter/lib/src/rendering/sliver_list.dart:40>):
  локальная запись display lists и материализация видимого диапазона списка.
- [Android vsync](<D:/Program Files/FlutterSDK/flutter/engine/src/flutter/shell/platform/android/vsync_waiter_android.cc>),
  [FlutterView](<D:/Program Files/FlutterSDK/flutter/engine/src/flutter/shell/platform/android/io/flutter/embedding/android/FlutterView.java:747>)
  и [Windows text input](<D:/Program Files/FlutterSDK/flutter/engine/src/flutter/shell/platform/windows/text_input_plugin.cc>):
  правильные границы platform adapter: timing, insets, IME, editing, accessibility.

Установлены Android platforms 31/33/34/35/36, sources android-36 и NDK
28.2.13676358/29.0.14206865. В NDK уже есть `android/choreographer.h`: можно
организовать C++ callbacks кадров без переноса UI на Java. При реализации
сохранить текущий minSdk=28: новые callback/timeline API требуют проверки
доступности и fallback. Для IME/accessibility тонкий Java/JNI bridge допустим;
layout и отрисовка остаются в C++.

## Отдельная находка: Standalone Impeller

В [toolkit/interop](<D:/Program Files/FlutterSDK/flutter/engine/src/flutter/impeller/toolkit/interop/README.md>)
есть автономный C API и C++17 wrapper `impeller.hpp`, без необходимости Dart UI.
Это реальный вариант экспериментального внешнего Canvas backend или эталона.
Однако:

- готовая `flutter_windows.dll` — Flutter embedder, не автономная замена нашему RHI;
- `impellerc.exe` — shader compiler, не библиотека отрисовки;
- локальных standalone Impeller binaries в просмотренном engine cache не найдено;
- standalone API документирован как не имеющий гарантий стабильности;
- GLES example требует подходящего current context и умеет оборачивать FBO;
  совместимость с нашим Windows WGL/OpenGL 4.6 и владение Vulkan-объектами надо
  проверить отдельно, а не предполагать drop-in совместимость.

Ни Impeller, ни Skia не подключены в этом исследовании. Автоматическая замена
нашего RHI изменила бы согласованную архитектуру. Рабочий план ниже сохраняет
свой renderer; подключение внешнего — отдельное решение и небольшой spike.
При заимствовании кода сохранять upstream notices и учитывать лицензии всех
зависимостей, а не только корневой Flutter LICENSE.

## Предлагаемый порядок разработки

### 1. Базовое качество и тесты стыков

Первый конкретный этап: fractional clipping, границы paint/clip и test scene.
Проверить одинаковые соседние заливки без AA внутренних общих краёв, границы
карточек, вложенные прямоугольные/скруглённые clip, тонкие линии и прозрачность.
Добавить GPU readback для обоих RHI. Исправление должно не допускать утечек
за clip и не ухудшать текущие DPI-тесты текста.

### 2. Полноценный 2D Canvas над RHI

Сначала определить Paint, Path, transform stack, clip stack и display list.
Затем расширить RHI: offscreen render targets, sample count/resolve,
depth/stencil, несколько passes и корректные переходы ресурсов. Добавить
saveLayer/group opacity, blur/shadow и осознанную color policy. MSAA 4x —
кандидат для поддерживающих устройств; нужен capability check и fallback.
Не включать дорогие слои и дополнительные проходы без необходимости.

### 3. Типографика и быстрые изменения

Выделить TextLayout/GlyphRun/FontCollection. Для полноценного текста рассмотреть
HarfBuzz + FreeType + Unicode processing либо более крупный paragraph backend.
[HarfBuzz](https://harfbuzz.github.io/what-is-harfbuzz.html) выбирает и размещает
глифы, [FreeType](https://freetype.org/) растеризует шрифты; bidi, переносы и
работа с абзацами [не решаются одним HarfBuzz](https://harfbuzz.github.io/what-harfbuzz-doesnt-do.html).
Окончательный выбор зависимостей — отдельный этап с проверкой сборки/размера.
GPU atlas потребует безопасных частичных uploads, поколений ресурсов и учёта
команд в полёте, которых текущий immutable RHITexture API ещё не предоставляет.

Добавить repaint boundaries, стабильные cached display lists, lazy lists,
dirty layout/paint и бюджеты кэша. Предварительно подготовленные шейдеры в Beast
уже есть; GPU pipeline creation/первую загрузку ресурсов всё равно измерять.

### 4. Поведение полноценного приложения

Frame scheduler с Android Choreographer, backpressure и 60/90/120 Hz там, где
это поддерживается. Далее IME/TextField, selection, clipboard, accessibility,
navigation, focus, анимации, изображения и нужный набор widgets. Эти функции
нельзя получить одним новым GPU backend.

### Критерии приёмки

- Обе ОС и оба RHI; настоящий Android device обязателен для runtime/frame pacing.
- Разрешения от узкого телефона до 4K; дробные DPI, resize, rotation,
  surface recreation, вложенный scroll, масштабирование и перемещение.
- Зафиксированные fonts/assets/colors и маленькие golden-сцены; отдельная
  допустимая погрешность AA, а не обещание побитового совпадения всех GPU.
- CPU layout/paint, GPU/present, p50/p95/p99, dropped frames, uploads и память
  измерять отдельно. 16.67/11.11/8.33 мс — бюджеты 60/90/120 Hz, не текущий результат.

## Завершение предыдущей задачи Visual Studio

Генератор теперь задаёт BeastSdkRoot, C++20, UTF-8 и определения Debug/Release
до импорта Microsoft.Cpp.props, который переносит их в Makefile ClCompile metadata.
EcoSbor.sln и SDK reference project перегенерированы. Проверены настоящие
GetClCommandLines/GetProjectDirectories для обоих проектов и конфигураций.
Сохранён повторяемый тест:

```powershell
.\Tests\Test-VisualStudioIntelliSense.ps1
```

Все четыре проверки PASS. Это проверка design-time параметров, не наблюдение
за списком ошибок в открытой Visual Studio. Если там остались старые сообщения,
следует перезагрузить solution/дождаться повторного разбора. Кэш `.vs` не удалялся.
