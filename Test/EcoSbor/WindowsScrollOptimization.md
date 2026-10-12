# Scroll/render changes — 2026-09-21

После сообщения пользователя об отсутствии доступа к телефону дальнейшие проверки
проводились на Windows. Итоговые Android input/pacing изменения на устройстве
не проверены: успешная компиляция APK не заменяет этот замер.

## Что изменено

- UI Application автоматически включает ограниченный idle-прогрев восьми дробных
  фаз видимого текста в Subpixel scroll. Выбор новых задач только в неподвижном
  UI; выполнение — без активного pointer/animation repaint. Вложенные scrolls
  учитывают обе оси. Прогрев прекращается после заполнения кеша, а не работает вечно.
- Используется прежняя native-resolution растеризация, nearest sampling и COW
  страниц. Готовые страницы заранее проходят обычный RHI upload через вырожденный
  треугольник без покрытия. Старые command lists сохраняют старое содержимое.
- Vulkan переиспользует только освобождённые nearest RGBA8 bitmap resources,
  после предыдущего frame fence. Отдельный пул: 8 MiB / 16 изображений. Размер
  должен совпадать; занятые текстуры и render targets в этот пул не попадают.
- Сенсорная позиция семплируется на время кадра: задержка 8 ms для интерполяции,
  прогноз не более 8 ms / 8 dp. Сопротивление overscroll считается по реальным
  событиям; прогноз не накапливается в raw-позиции и не меняет оценку fling speed.
  TouchUp/Cancel прекращают sampling, spring начинается с текущей позиции без snap.
  Clamping не выходит за границы. Mouse/wheel этот sampler не используют.
- Android запрашивает следующий Choreographer callback перед рендером, сохраняя
  single-pending и epoch/lifecycle защиту. Это нуждается в проверке на телефоне.
- Windows больше не добавляет целые 16 ms после блокирующего Present. Оставшаяся
  часть бюджета ожидается через локальный высокоточный waitable timer с обработкой
  сообщений, без spin loop и глобального timeBeginPeriod. При недоступности API
  остаётся обычный таймер/fallback. Target пока 60 Hz, не adaptive refresh rate.
  API: [Microsoft CreateWaitableTimerExW](https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-createwaitabletimerexw).

## Воспроизводимые проверки

`Tests/Run-Tests.ps1`: strict MSVC /W4 /WX, EcoSbor CPU + реальные OpenGL/Vulkan
окна, resampling обеих осей, raw integration, release/cancel/rest, clamp bounds,
старый pixel-aligned settle probe и WarmupBench.

`BeastSDK/Tests/UIGlyphAtlasTests.cpp`: exact coverage/reference, COW, две оси,
восемь фаз, sealed uploads, cancellation и completion. `RHIResourceTests.cpp`:
texture bursts, replay, retirement, nested layers и device recreation с validation.

`WarmupBench` делает первый overscroll EcoSbor при scale=2.625, без прогрева и
после idle-прогрева. Для протестированного набора видимых элементов:

| Работа во время жеста | Без прогрева | С прогревом |
| --- | ---: | ---: |
| Glyph misses | 1344 | 0 |
| Новые версии страниц | 54 | 0 |
| CPU p95, Windows Debug (один прогон) | 14.490 ms | 7.503 ms |
| CPU median | 1.165 ms | 1.781 ms |

Это не измерение Android/GPU и не доказательство ускорения каждого кадра.
Тест проверяет снижение cold work и возвращение к idle, но не требует конкретных
миллисекунд на любой машине. Прогрев измеряется отдельно от жеста; мгновенный ввод
сразу после запуска, новые строки, смена DPI и eviction могут снова дать misses.

`BeastSDK/Tests/WindowPacingTests.cpp` — 120 интервалов OnUpdate после 15 warm frames,
реальное окно + vsync, не точные display presentation timestamps. Первая версия
с обычным Win32 timeout: median GL 24.534 / Vulkan 24.351 ms, p95 31.821 / 31.962 ms.
Высокоточный таймер: median 17.001 / 17.073 ms, p95 17.395 / 17.389 ms.
Остались единичные max 80.567 / 42.277 ms — причины этих выбросов не установлены;
не следует утверждать, что все лаги исчезли. Проверка качества — на EcoSbor,
а clear-only pacing test изолирует Windows scheduling от сложного UI.

Итоговый интерактивный файл: `Binaries/Windows/Release/EcoSbor.exe`.
Backend в Launch.cpp сохранён Vulkan. OpenGL отдельно покрывается GPU-тестами.

## Уточнение: переход drag → spring (21 сентября 2026)

Отдельные детерминированные тесты воспроизвели две ошибки движения, независимо
от времени GPU-рендеринга:

- Если release приходил за 1 ms до очередного кадра, пружина всё равно получала
  весь frame delta. В исходном сценарии при 30 Hz ошибка первой позиции была
  8.545326 dp. Теперь первый шаг ограничен возрастом release; следующие шаги
  обычные. Mouse/touch, обе оси, release/cancel, 30/60/120 Hz проверяются против
  отдельно интегрируемой физики с допуском 0.001 dp.
- После 80 ms без move отпускание сохраняло старую скорость. За первую 1 ms
  список смещался ещё на -0.836823 dp наружу вместо возврата. Теперь скорость
  плавно затухает между 20 и 80 ms неподвижного удержания; быстрый fling сохранён.

Дополнительно подавлено повторное включение hover под курсором во время drag.
Регрессии `UIScrollHostTests`: будущий timestamp, обычный fallback без clock,
повторный захват, программный Fling и независимое время дочерних анимаций.
Изменения находятся в SDK; проекту не нужны новые настройки/обработчики.

Это исправления скачков траектории, не доказательство отсутствия всех пропусков
кадров. Телефон недоступен; поведение на физическом Android после этих изменений
не проверялось.
