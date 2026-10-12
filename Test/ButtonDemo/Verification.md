# Beast UI verification — 2026-09-12

## Компоновка: Row / Column / Expanded / Container — 2026-09-12

`Run-UITests.ps1` — PASS для всех трёх наборов, MSVC C++20 `/W4 /WX`.
Windows/Android Debug продукты собраны; сборка самого BuildTool — 0 warnings/errors.

- Новый `UILayoutTests`: обе оси, веса 1:2/2:1, 90 комбинаций дробных размеров
  и весов, нулевой остаток и overflow, вложенные Flex и отказ при бесконечной
  главной оси, неверном родителе Expanded и нулевом весе; большие unsigned веса.
- Шесть основных выравниваний, cross-axis Stretch/End/Center, пустые/одиночные
  строки, shrink-wrap и сохранение прежних настроек Column.
- Container: четыре отступа, девять alignment, width/height/min/max и приоритет
  родителя, пустое содержимое, схлопывание чрезмерных отступов, неверные стили.
- Динамическая перекомпоновка отменяет незавершённый pointer/keyboard click,
  если bounds кнопки изменились; смена только цвета сохраняет нажатие.
- Полный ButtonDemo: 30 сочетаний узких/широких/низких viewport и DPI 1/2,625;
  сохранение пропорций Expanded и равных размеров кнопок, без ошибки при clipping.
- `UITests` повторно проверил ввод и новую сцену на реальных Windows OpenGL/Vulkan:
  25 кадров на API, resize, смена счётчика/пропорций, пересоздание RHI.
  Vulkan validation включён, ошибок/предупреждений в журнале нет.
- `UIRasterTests` повторно прошёл CPU-разрешения до 4K и 9 DPI на обоих API.
  OpenGL GPU readback совпадает с AA-текселями в пределах 1/255; отдельного
  Vulkan readback в Windows нет. `windows-opengl.bmp` теперь показывает новую сцену.
- Android: новый экран установлен и запущен на OpenGL ES и Vulkan, штатные
  1080×2400 и scale 2,625. Начальные кадры сравнили на сетке 44 850 точек:
  max RGB delta 0. Это совпадение конкретной сцены/эмулятора, не всех GPU.
- Vulkan: tap → счётчик 1, пропорции 2:1; Home → resume в том же PID 4496,
  новый RHI и сохранённая сцена. OpenGL ES: один click, disabled и drag не
  добавили нажатий; Home → resume сохранил счётчик 1 и пропорции 2:1 в PID 5258.
  Android validation layer не установлен. После тестов установлен итоговый Vulkan
  APK; запущенный нами headless-эмулятор закрыт без удаления данных AVD.

Новые подтверждения в `Intermediate/Verification`:
`UILayoutTests.log`, `UITests.log`, `UIRasterTests.log`,
`android-layout-vulkan.png`, `android-layout-vulkan-resumed.png`,
`android-layout-opengl.png`, `android-layout-opengl-resumed.png`, `android-layout-vulkan.log`,
`android-layout-opengl.log`, `android-layout-comparison.log`.

Стандартные EXE/APK и снимки OpenGL/Vulkan обновлены для новой компоновки.
Исходный enum и стандартные продукты оставлены на Vulkan; .sln перегенерирован,
`UI/Layout.cpp` включён в проект/фильтры SDK. Скругление Container пока декорирует
фон, а не clip детей; Wrap/Scroll/RTL/baseline и полноценная система стилей ещё
не реализованы. Ниже сохранены подробности предыдущих этапов проверки.

## Чёткость при дробном DPI — 2026-09-11

После устранения повторного масштабирования текста `Run-UITests.ps1` запускает
также `UIRasterTests.cpp`. Оба набора — PASS; компиляция тестов `/W4 /WX`.

- CPU-проверка отображения тексель→пиксель для 800×600, 1920×1080, 2560×1440,
  3840×2160; scale 0,75 / 1 / 1,25 / 1,5 / 1,75 / 2 / 2,625 / 3 / 4.
- Целочисленные границы quad и его физические размеры равны размерам bitmap.
  Растеризация сверяется с точным дробным em, а не округлённым размером.
- Проверены дробные/отрицательные позиции, safe-area offsets, кеш при неизменном
  DPI и его разделение при малом изменении DPI/фазы, шрифт 512 физических px
  без старого увеличения 256px-текстуры, а также em менее одного пикселя.
- Реальный Windows GPU: 9 масштабов на OpenGL и Vulkan. OpenGL FBO readback
  сравнивается с исходными AA-текселями (погрешность не более 1/255 на канал).
  Vulkan прошёл runtime с validation без сообщений об ошибках; отдельного
  Vulkan pixel readback в этом тесте нет. Это искусственно заданные UI scale,
  а не переключение DPI монитора средствами ОС.
- Журнал: `Intermediate/Verification/UIRasterTests.log`.
- Кадры Windows до/после: `windows-opengl-before-native-text.bmp` и
  `windows-opengl.bmp` в той же папке. Они просмотрены в исходном разрешении.
- Android 1080×2400, штатный scale 2,625: обновлённые OpenGL ES/Vulkan APK
  установлены и запущены. Кадры `android-native-text-opengl.png` и
  `android-native-text-vulkan.png` просмотрены; повторное сравнение 44 850 точек
  дало max RGB delta 0 (`android-native-text-comparison.log`).
- Vulkan после нажатия и Home → resume восстановил текст «Нажатий: 1» в том же
  PID 4241. Подтверждение: `android-native-text-vulkan.log` и
  `android-native-text-resumed.png`. Android validation layer не установлен.
- Стандартные EXE/APK и снимки OpenGL/Vulkan обновлены с native-size текстом.
  Enum в исходниках и стандартные продукты оставлены на Vulkan.

Эти тесты не означают отсутствие естественного сглаживания или гарантию чёткости
после масштабирования готового изображения самим эмулятором/монитором.

## Автоматические проверки Windows

`./BeastSDK/Tests/Run-UITests.ps1` после Windows Debug build — PASS.
Тест компилируется MSVC C++20 с `/utf-8 /W4 /WX`.

- Constraints и неверные параметры, дерево без циклов/повторного владения.
- Неверные Canvas color/font/radius/clip и вызовы рисования вне recording.
- UTF-8, кириллица, замена некорректной последовательности, premultiplied coverage.
- Одинаковые логические bounds при scale 1/2, корректные safe-area hit coordinates.
- Mouse hover, click exactly once, drag outside, capture loss, MouseLeave.
- Touch IDs, игнорирование второго пальца, cancel, slop >18 dp, отпускание после
  скачка без move. Disabled не получает click.
- Focus/surface loss, suspend, изменение DPI и disable во время жеста отменяют его.
- Tab/Shift+Tab, пропуск disabled, Space key repeat, callback с заменой root.
- Реальные Windows GPU: по 25 кадров OpenGL/Vulkan, обновление счётчика,
  изменение размера окна и полное пересоздание RHI с сохранением UI Context.

OpenGL 4.6 — AMD driver 25.10.36.08.260506. Для Vulkan на AMD Radeon Graphics
включён VK_LAYER_KHRONOS_validation; ошибок/предупреждений в журнале UITests нет.
Файл `Intermediate/Verification/windows-opengl.bmp` получен чтением реального
offscreen OpenGL framebuffer, не из программной имитации; проверено наличие
отрисованных пикселей и визуально — текст/кнопки. Vulkan Windows прошёл runtime
и validation, но отдельный pixel readback для него не выполнялся.

CoreTests с обновлённой библиотекой также PASS: ввод и Windows lifecycle.

## Android runtime

Эмулятор Medium_Phone_API_36.1, API 36.1, 1080×2400, host GPU.
APK arm64-v8a, NDK 29.0.14206865, NativeActivity; на эмуляторе используется
имеющаяся трансляция ARM64. Физические телефоны в этой проверке не участвовали.

| Проверка | OpenGL ES | Vulkan |
| --- | --- | --- |
| Сборка и установка APK | PASS | PASS |
| Первый кадр с текстом и кнопками | PASS | PASS |
| Реальный tap увеличивает счётчик один раз | PASS | PASS |
| Disabled и drag не увеличивают счётчик | PASS | PASS |
| Home → resume сохраняет счётчик, RHI пересоздаётся | PASS | PASS |

Android OpenGL: ES 3.1 / NVIDIA 596.13. Vulkan: Goldfish GFXStream,
NVIDIA GeForce RTX 5060 Laptop GPU. Android validation layer отсутствует;
соответствующее информационное сообщение ожидаемо, не выдаём этот запуск
за проверку с validation.

До любых нажатий сравнивались кадры OpenGL/Vulkan с одинаковой сценой: сетка
с шагом 7 пикселей, x=[16,1064), y=[160,2250), системные панели исключены.
Все 44 850 сравниваемых RGB пикселей совпали точно (max delta 0).
Это проверка конкретного экрана/эмулятора, не гарантия побитового совпадения
на всех GPU/DPI. Между ОС сравнивается общий layout в dp, а не физические размеры.

Первая попытка дополнительной проверки adb была остановлена лимитом сервиса
разрешений. После сброса лимита тест продолжен. Старый headless-эмулятор после
нескольких часов работы возвращал чёрные снимки целиком; в его host-журнале были
ошибки восстановления GL-контекста и импорта внешней GPU-памяти. Для финальной
проверки тестовый эмулятор перезапущен без удаления пользовательских данных.
После перезапуска итоговый OpenGL APK снова показал нормальный экран, а Vulkan
прошёл tap/disabled/drag и Home → resume: тот же PID 3995, новый RHI и на экране
«Нажатий: 1». После проверки наш headless-экземпляр эмулятора закрыт.
Причина сбоя host GPU после длительного простоя отдельно не установлена;
перезапуск устранил его в этом тесте, устойчивость длительных сессий не заявляется.

## Артефакты

- `Intermediate/Verification/UITests.log` — последний полный UI test run.
- `Intermediate/Verification/android-opengl.log` — click 1, затем повторный
  OpenGL context/frame в том же PID 4621 после возвращения из фона.
- `Intermediate/Verification/android-opengl.png`, `android-vulkan.png` — первые
  кадры без нажатий; `android-pixel-comparison.log` — сравнение пикселей.
- `Intermediate/Verification/android-disabled.png` — primary отключён.
- `Intermediate/Verification/android-opengl-resumed.png` — счётчик 1 после resume.
- `Intermediate/Verification/android-opengl-final.png` — повторный запуск
  окончательного OpenGL APK на перезапущенном эмуляторе.
- `Intermediate/Verification/android-vulkan.log`, `android-vulkan-resumed.png` —
  окончательная проверка Vulkan resume со счётчиком 1.
- EXE/APK в стандартных Binaries путях — Vulkan, соответствуют текущему enum.
- Подкаталоги OpenGL/Vulkan — снимки сборок для проверки API, оба обновлены
  после окончательных изменений Canvas. Для обновления снимка переключите enum,
  выполните build и скопируйте продукт; обычный build эти подкаталоги не меняет.

Ещё не проверены: iOS/Linux/macOS (адаптеров нет), реальные Android-устройства,
accessibility, сложная типографика, большие списки и performance/энергопотребление
под нагрузкой. Это первый этап UI, не полный Flutter SDK.
