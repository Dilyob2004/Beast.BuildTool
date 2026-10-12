# Beast RHI — первый графический слой

Одна библиотека Beast содержит два бэкенда:

| Платформа | OpenGL | Vulkan |
| --- | --- | --- |
| Windows | OpenGL 4.6 core, WGL | Vulkan 1.0 + KHR swapchain/Win32 surface |
| Android | OpenGL ES 3.0, EGL | Vulkan 1.0 + KHR swapchain/Android surface |

На Windows запрашивается OpenGL 4.6 Core, затем проверяются фактическая версия
и профиль контекста. Отката на 4.5/3.3 нет: неподдерживаемый GPU/драйвер вызывает
понятную ошибку создания RHI. Android использует отдельный API OpenGL ES;
требование desktop OpenGL 4.6 на него не переносится.

Vulkan-реализация использует Vulkan-Hpp: `vk::Image`, `vk::ImageView`,
`vk::CommandBuffer`, `vk::RenderPass`, `vk::SwapchainKHR`. C ABI используется
только на границе загрузчика. У каждого экземпляра свой dynamic dispatcher,
глобального изменяемого Vulkan dispatcher нет.

## Использование

Подключение: `#include "Beast.h"` либо `#include "RHI/RHI.h"`.
В Configure установите `WindowDefinition::gpuRendering = true`, чтобы платформа
не рисовала диагностический CPU-буфер поверх GPU-поверхности.

В обработчике SurfaceCreated:

```cpp
Beast::RHIOptions options;
options.backend = Beast::RHIBackend::Vulkan; // или OpenGL
options.vsync = true;
options.validation = true; // если Vulkan validation layer установлен
renderer = Beast::RHI::Create(GetWindow(), options);
```

На каждом кадре:

```cpp
commands.Reset();
commands.BeginRenderPass({0.06f, 0.08f, 0.13f, 1.0f});
commands.ClearRect({100, 80, 240, 120}, {0.1f, 0.7f, 0.9f, 1.0f});
commands.EndRenderPass();
renderer->Submit(GetWindow(), commands);
```

Для простой очистки всего кадра без прямоугольников:

```cpp
commands.ClearFrameBuffer({0.1f, 0.1f, 0.1f, 1.0f}); // Пустой список → закрытый clear-only pass.
renderer->Submit(GetWindow(), commands);
```

При постоянном цвете этот список можно записать один раз и повторно Submit на
каждом кадре. Для нового цвета сначала вызовите Reset. ClearFrameBuffer во время
активного/закрытого прохода отклоняется, как повторный BeginRenderPass.
Пример `Test/DemoApp` выбирает бэкенд через `constexpr RHIBackend Backend`
в `Source/Launch/Launch.cpp`; его .sln поддерживает Windows и Android.

`RHICommandList` записывает команды на CPU; вызовы GPU происходят при Submit.
Один список содержит один проход в текущую поверхность. Закрытый список можно
повторно отправлять; Reset начинает новую запись. Неверная последовательность
вызывает `std::logic_error`, неверные размеры/цвета — `std::invalid_argument`.
ClearRect заменяет цвет области (без blending), координаты отсчитываются от
левого верхнего угла в физических пикселях и обрезаются по размеру поверхности.
Цвета конечные, в диапазоне [0, 1]; полноценного управления цветом пока нет.

Создание, Submit и уничтожение RHI выполняются на потоке Run.
В SurfaceDestroyed обязательно вызовите `renderer.reset()` до возврата из
callback: нативный handle после него недействителен. На Android при возврате из
фона новый SurfaceCreated создаёт новый RHI. Изменение размера обрабатывается
внутри Submit. Возврат false означает пропущенный кадр при resize/recovery
либо ещё не завершённую работу GPU; обработку событий нужно продолжать,
а Submit повторять на следующем обновлении, не в блокирующем цикле ожидания;
фатальные ошибки драйвера/потери устройства выбрасывают исключение.
EGL восстанавливает потерянный drawable; Vulkan обрабатывает out-of-date,
suboptimal и потерю поверхности. Автоматическое восстановление VkDevice после
device-lost пока не реализовано.

## Реализация и зависимости

- `Private/Windows`: WGL и загрузчик vulkan-1.dll / Win32 surface.
- `Private/Android`: EGL/GLES loader и libvulkan.so / Android surface.
- `Private/OpenGL`: исполнение в FBO через glClear/glScissor, blit в окно, GLsync и present.
- `Private/Vulkan`: GPU render pass, clear attachments, acquire/submit/present.

Vulkan использует один CPU frame-in-flight, fence для command buffer и
отдельный presentation semaphore на каждое изображение swapchain. Пересоздание
и завершение ждут device idle; это простая базовая реализация без асинхронного
retirement старых swapchain. Требуется общая очередь graphics/present.

Текущий Vulkan backend создаёт `vk::Framebuffer` для каждого изображения
swapchain и использует `vk::Fence` для завершения отправленных команд.

OpenGL на Windows и OpenGL ES на Android используют общий `OpenGLFrameResources`:

- Внутренний FBO с одним `GL_RGBA8` renderbuffer, размером с нативный drawable.
  Depth/stencil и MSAA пока не выделяются. Это приватный attachment, не публичная текстура.
- Команды исполняются в FBO, затем `glBlitFramebuffer` копирует весь цветовой
  буфер в оконный framebuffer (0). Scissor отключается перед копированием;
  дополнительного переворота изображения нет.
- `glFenceSync` после blit и `glFlush` отправляют работу. Перед повторным
  использованием/resize проверяется предыдущий fence через `glClientWaitSync`
  с таймаутом 1 мс. По timeout кадр пропускается, чтобы продолжить обработку событий.
  Один незавершённый GPU-кадр; `glFinish` в кадровом пути не используется.
- FBO создаётся только при первом кадре и изменении размера. Проверяются лимит
  renderbuffer, ошибки выделения и `GL_FRAMEBUFFER_COMPLETE`; неудачная замена
  не удаляет предыдущий исправный target.
- При уничтожении RHI текущий контекст освобождает FBO/renderbuffer/sync. OpenGL
  сохраняет хранилище, нужное уже поставленным в очередь командам. При потере
  EGL-контекста меняется generation: старые handles забываются без вызовов Delete
  в новом контексте, таблица функций загружается заново, target создаётся на следующем кадре.
  Это учитывается и при исключении во время восстановления контекста.

Этот fence отмечает завершение GL-команд, а не момент показа кадра композитором.
Это основа синхронизации, не законченная система frame pacing для 60/120 Гц.
FBO добавляет полнокадровое копирование и дополнительный цветовой буфер;
его стоимость, особенно на мобильных GPU, нужно профилировать на UI-нагрузках.
Публичные шейдеры, текстуры, graphics pipeline и DrawTriangles описаны в
[Resources.md](Resources.md). Пользовательские fences пока не экспортируются.

Заголовки Khronos Vulkan-Headers/Vulkan-Hpp (VK_HEADER_VERSION 328) скопированы
без изменений из `Prototypes/Source/ThirdParty/NVRHI/nvrhi_v22/thirdparty/Vulkan-Headers`
в `BeastSDK/ThirdParty/Vulkan-Headers`, вместе с лицензиями Apache-2.0/MIT.
Vulkan SDK на машине сборки clear-only приложений не обязателен; runtime loader предоставляет драйвер/ОС.
Для HLSL-шейдеров нужны host-инструменты DXC, SPIRV-Cross и spirv-val (например, из Vulkan SDK).
Заголовки включаются в поставку сборщика. GL/WGL/EGL headers подключены из
`BeastSDK/ThirdParty/OpenGL`; GLES3 headers для Android предоставляет NDK.
Код Unreal из `Prototypes/OpenGLDrv` и `Prototypes/VulkanRHI` не включён в библиотеку:
использованы архитектурные ориентиры загрузки функций и жизненного цикла контекстов.

Документация Khronos: [Vulkan-Hpp configuration](https://github.com/KhronosGroup/Vulkan-Hpp/blob/main/docs/Configuration.md),
[swapchain semaphore reuse](https://docs.vulkan.org/guide/latest/swapchain_semaphore_reuse.html).
Правила framebuffer/blit/sync: [спецификация OpenGL ES 3.0](https://registry.khronos.org/OpenGL/specs/es/3.0/es_spec_3.0.pdf).

### Загрузка OpenGL

`Private/OpenGL/OpenGLFunctions.h/.cpp` содержит общую типизированную таблицу
функций. Макросы `BEAST_GL_ENTRYPOINTS_BASE` и `BEAST_GL_ENTRYPOINTS_CORE`
задают пары `(PFNGL...PROC, glFunction)`: один список используется и для
объявлений, и для загрузки с проверкой. Здесь перечислены функции текущего RHI,
а не все функции Unreal-прототипа. Новые обязательные функции нужно добавлять
в список согласно минимальной версии API; расширения требуют отдельной
проверки поддержки и не должны становиться обязательными неявно.

`Private/Windows/WindowsOpenGLFunctions.h/.cpp` отдельно загружает WGL:

1. LoadLibrary для opengl32.dll и GetProcAddress для базовых WGL/GL функций.
2. Bootstrap-контекст, затем wglGetProcAddress для создания core-контекста.
3. Новый current-контекст, повторная загрузка WGL-указателей и GL-таблицы.
4. Проверка WGL_EXT_swap_control перед использованием опционального swap interval.

Нулевые адреса и WGL-сентинелы 1, 2, 3, -1 не считаются функциями. Недостающий
обязательный entry point вызывает ошибку с его именем. GL-таблица при ошибке
остаётся полностью пустой. Таблицы принадлежат экземплярам, не глобальны.
После восстановления EGL-контекста GL-таблица загружается заново, а старые
frame resources больше не используются и не удаляются через новый контекст.
На Android используется dlsym из libGLESv2.so с fallback на eglGetProcAddress.

Файлы в ThirdParty — объявления типов/констант, не реализация OpenGL:
фактический код функций предоставляет драйвер/ОС. Исходные заголовки пользователя
не изменены. `GL/glcorearb.h` не смешивается с `GL/gl.h` или `GL/glext.h`.

Текущий пользовательский `glcorearb.h` содержит объявления до OpenGL 4.5.
Этого достаточно для создания 4.6-контекста через WGL и нынешних команд;
перед использованием новых entry points/констант именно 4.6 потребуется
обновить заголовок из Khronos Registry. Версия заголовка не задаёт версию контекста.

`Tests/OpenGLLoaderTests.cpp` проверяет раздельные BASE/CORE пути,
сбой обязательных entry points, отсутствие частичной таблицы после ошибки,
Reset/повторную загрузку и независимость двух таблиц (без реального GPU).
Проверка отсутствующих entry points теперь проходит по каждому элементу таблицы,
включая FBO/renderbuffer, blit и sync-функции.

`Tests/OpenGLFrameTests.cpp` проверяет создание/повторное использование/resize,
порядок blit → fence → flush, timeout/ошибки fence, ошибки выделения/completeness,
сохранение старого target при ошибке и context-loss/reload (включая отказ recovery).
`Tests/OpenGLFrameGpuTests.cpp` на Windows проверяет реальные FBO, RGBA8/размеры,
пиксели, blit, resize и пересоздание. Проверка пикселей окна отдельно калибруется
прямой очисткой default framebuffer: при недоступном оконном readback явно
выводится SKIP, а offscreen-проверки продолжаются. Для этих тестов к команде
компиляции ниже добавьте `/I BeastSDK\ThirdParty\OpenGL` и замените имя исходника/выходных файлов.

## Примеры и границы

`Test/DemoApp` проверяет очистку кадра, `Test/ResourcesDemo` — текстуры и pipeline;
оба выбирают API через `constexpr RHIBackend Backend` в Launch.cpp.
В шаблоне Launch.cpp выбор Vulkan задаётся определением `BEAST_RHI_VULKAN`
в Launch.Build.cs либо прямым изменением options.backend. Неявного fallback нет:
если выбранный API недоступен, причина выводится в журнал.

`BeastSDK/Tests/RHITests.cpp` проверяет запись команд, а на Windows — оба
бэкенда, resize и пересоздание RHI на существующем HWND. После Debug-сборки ResourcesDemo,
из x64 Developer Command Prompt в корне репозитория:

```bat
cl /nologo /std:c++20 /EHsc /MDd /W4 /WX /I BeastSDK\Source BeastSDK\Tests\RHITests.cpp /FoTest\ResourcesDemo\Intermediate\RHITests.obj /FeTest\ResourcesDemo\Intermediate\RHITests.exe Test\ResourcesDemo\Intermediate\Build\BeastSDK\Windows\Debug\Beast.lib user32.lib gdi32.lib
Test\ResourcesDemo\Intermediate\RHITests.exe
```

Это ещё не законченный UI-рендерер. Поддерживаются vertex/fragment shaders,
RGBA8-текстуры, pipeline, triangle-list и alpha blending. ClearRect по-прежнему
исполняется как GPU clear; DrawTriangles использует настоящий shader pipeline.
Index buffers, uniform/storage buffers, depth/MSAA, изменяемые render targets,
render thread и GPU memory allocator — следующие расширения.

### Неизменяемые offscreen-слои

`RHI::CreateLayerTexture(width, height, closedCommandList)` создаёт текстуру-снимок
законченного списка команд. Рендеринг выполняется лениво на GPU при первом
использовании в DrawTriangles; результат кешируется на время жизни handle.
GetContent возвращает неизменяемый исходный список, GetDesc().pixels пуст.
CPU-текстуры через CreateTexture продолжают работать без изменений.

Внутренний `Private/TextureBuilder.h` собирает страницы UI-атласа на одном
потоке до `Seal()`. Запись команд может ссылаться на такой handle, но Submit
обоих backend отклоняет незапечатанную текстуру, в том числе внутри слоя.
`Canvas::End` запечатывает страницы перед возвратом готового кадра. После Seal
записи запрещены; дополнение страницы создаёт копию с новым handle. Это не
публичный API изменяемых текстур: обычный `CreateTexture` сразу неизменяем,
сохранённые списки команд и слои никогда не получают новое содержимое задним числом.

Формат RGBA8, premultiplied alpha, UV (0,0) соответствует верхнему левому углу
на обоих backend, фильтр Nearest. Clear и ClearRect внутри слоя должны быть
premultiplied, как и итог шейдеров при последующей композиции. Снимки могут
ссылаться только на уже созданные handle — циклы и чтение из текущего target
через этот API невозможны. Лимиты: 4096×4096, 8 вложений, 256 MiB/кадр.

OpenGL использует текстуру и FBO, восстанавливая framebuffer,
viewport и scissor после вложенного прохода. Vulkan записывает offscreen
render passes в порядке зависимостей перед основным проходом, в тот же command
buffer/submit; переход ColorAttachment → ShaderRead обеспечен render pass.
Отдельного queue.waitIdle для слоёв нет. Root/offscreen pipelines кешируются
раздельно, поскольку swapchain и слой могут иметь разные форматы.
После пересоздания RHI GPU-снимки восстанавливаются из CPU-описания.

### Переиспользование памяти слоёв и диагностика

Пул включён автоматически, приложение не должно управлять native handles.
После завершения fence кадра ресурсы **освобождённых CPU-снимков** попадают
в пул и могут обслужить новый слой того же размера. Живой handle и сохранённый
список команд никогда не переиспользуются под чужое содержимое. Новый слой
обязательно очищается и рисуется заново; это пул памяти, не RepaintBoundary.

- Exact match width/height, RGBA8, тот же backend/device. Изменение размеров
  не растягивает старую картинку и не меняет пиксельную сетку.
- Сохраняются GL texture+FBO; Vulkan image+memory+view+sampler+descriptor+FBO.
  Не нужны повторные выделения и сборка framebuffer при совпадении размеров.
- Не более 16 свободных targets и 32 MiB по умолчанию. `RHIOptions.layerPoolBytes`
  задаёт бюджет свободной памяти (0 отключает пул, максимум 64 MiB). Более старые
  возвращённые элементы вытесняются первыми; слишком крупные не сохраняются.
  Vulkan учитывает размер memory allocation, GL — RGBA8 payload; служебная
  память драйвера/дескрипторов в число байтов не входит. Лимит активных слоёв
  256 MiB/кадр не изменён; свободный пул является отдельным ограниченным запасом.
- Нет дополнительных ожиданий GPU: используются существующие frame fences.
  При уничтожении RHI пул освобождается; при потере GL-контекста старые имена
  забываются без удаления в новом контексте.

Включить замеры: `RHIOptions.profiling = true`. Читать `rhi.GetFrameStats()`:
`submitCpuMs` — wall time последнего Submit, включая ожидания/presentation;
`gpuMs` — асинхронный результат для `gpuFrame`, обычно предыдущего кадра.
При чтении сравнивайте gpuFrame, чтобы не считать один результат несколько раз.
Без profiling CPU timer/GPU queries не выполняются; счётчики слоёв доступны.
`layers.allocations/reuses/renders/evictions` накопительные в пределах RHI/
поколения GL-контекста, `pooledBytes/pooledTargets` — текущее состояние пула.

GPU-замер охватывает интервал рисования слоёв и основного кадра, без presentation
(на GL также без финального blit). Это elapsed GPU interval со stalls, а не
сумма длительностей шейдеров или end-to-end latency. Разовое создание шейдеров,
текстур и нагрев GPU искажают первые кадры — benchmark их исключает.
GL использует две timestamp query; Vulkan — timestamp query pool, valid bits
очереди и timestampPeriod. Чтение только готовых результатов, без glFinish/
нового waitIdle/QUERY_RESULT_WAIT. Отсутствие поддержки, disjoint на GLES или
пауза дольше периода аппаратного счётчика дают unavailable, не фиктивные 0 ms.
На GLES требуется необязательный `EXT_disjoint_timer_query` с timestamp bits;
если его нет, приложение продолжает работать без GPU-таймера.

Правила таймеров сверены с [Khronos EXT_disjoint_timer_query](https://registry.khronos.org/OpenGL/extensions/EXT/EXT_disjoint_timer_query.txt)
и [Vulkan timestamp queries](https://docs.vulkan.org/samples/latest/samples/api/timestamp_queries/README.html).
Воспроизводимые замеры: `Test/EcoSbor/Tests/Run-RenderBench.ps1`;
сначала собрать EcoSbor Windows Debug, затем запускать без других GPU-тестов.

### Пакетная загрузка immutable-текстур Vulkan

`Prepare` объединяет новые текстуры (включая версии атласа глифов) в один
queue submit и одно ожидание на пакет, вместо ожидания после каждой текстуры.
Пакет ограничен 8 MiB staging payload и 128 изображениями; отдельная текстура
больше бюджета загружается отдельным пакетом. Выравнивание выделений и память
драйвера в payload не входят. Повторное использование готовых текстур ожиданий
загрузки не добавляет. Это ещё не асинхронный staging ring: ожидание пакета осталось.

Staging buffers/command buffers живут до завершения копирования. Ошибка подготовки
удаляет незагруженные cache entries, чтобы повторный кадр не использовал изображение
с неопределённым содержимым. Обход вложенных слоёв использует ту же очередь загрузки.
`RHIResourceTests` проверяет tiny-image limit, границы бюджета, большую текстуру,
вложенный слой, повторный Submit, освобождение ресурсов и пересоздание устройства.
