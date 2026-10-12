# Шейдеры, текстуры и pipeline — ABI v2 (совместим с v1)

Общий публичный API работает на Windows (OpenGL 4.6 core / Vulkan) и Android
(OpenGL ES 3.0+ / Vulkan). Это первый набор ресурсов для 2D/UI, не полный UI SDK.

## Один HLSL для двух RHI

Как в ShaderCore-прототипе, основной язык — HLSL. Компиляция перенесена в Beast
BuildTool, до C++-сборки: DXC создаёт SPIR-V 1.0 под Vulkan 1.0, `spirv-val`
проверяет бинарник, SPIRV-Cross делает reflection и GLSL 460 / GLSL ES 300.
NVRHI и зависимости старого движка не перенесены.

Объявите точки входа в модуле `Launch.Build.cs` (или другом `*.Build.cs`):

```csharp
Shaders.Add(new ShaderCompileRequest {
    Name = "UIVertex", Source = "Source/Launch/Shaders/UI.hlsl",
    EntryPoint = "VSMain", Stage = ShaderStage.Vertex
});
Shaders.Add(new ShaderCompileRequest {
    Name = "UIFragment", Source = "Source/Launch/Shaders/UI.hlsl",
    EntryPoint = "PSMain", Stage = ShaderStage.Fragment
});
// request.Defines["MY_MACRO"] = "1";
```

Source относителен корню проекта. Include ищется рядом с исходником и в Source.
Name — уникальный C++ identifier без начального `_` и не ключевое слово.
Полный рабочий HLSL находится в `Test/ResourcesDemo/Source/Launch/Shaders/UI.hlsl`.

Пути к host-инструментам задаются в `Config/BuildConfig.json`:

```json
"shaderTools": {
  "dxc": "C:/VulkanSDK/1.4.335.0/Bin/dxc.exe",
  "spirvCross": "C:/VulkanSDK/1.4.335.0/Bin/spirv-cross.exe",
  "spirvValidator": "C:/VulkanSDK/1.4.335.0/Bin/spirv-val.exe"
}
```

Это пример установленной версии, а не требование к версии SDK. Пустые значения
ищут инструмент в VULKAN_SDK/Bin и PATH; относительные пути — от проекта.
Настроенный отсутствующий инструмент вызывает ошибку, без молчаливого fallback.
Проект без объявлений Shaders не требует этих инструментов.

Артефакты: `Intermediate/Build/<Windows|Android>/<configuration>/Shaders/`.
Здесь лежат `.spv`, `.glsl`, `.gles`, reflection JSON, препроцессированный HLSL,
кеш и `BeastShaders.generated.h`. Каталог автоматически подключается C++-компилятору;
VS IntelliSense получает Windows-каталог, HLSL показывается в фильтрах как None,
без MSBuild shader compiler и wildcard items. После добавления файлов выполните generate.

В EXE/APK встраивается пакет всех трёх представлений шейдера. Ни DXC, ни
SPIRV-Cross на Android не запускаются; OpenGL-драйвер компилирует готовый GLSL при
первом использовании pipeline. Debug/Release сейчас используют оптимизацию HLSL
`-O3` (в том числе удаление ресурсов чужих entry points); shader debug symbols нет.

Кеш учитывает препроцессированный исходник, транзитивные include, макросы,
entry point/stage, аргументы и хеши инструментов/их compiler DLL. Проверяются
хеши выходных файлов. Повреждённый кеш пересобирается. Header обновляется только
при изменении содержимого; ошибки шейдеров останавливают сборку до компиляции C++.
Сохранённый прошлый header при ошибке не используется для продолжения сборки.

## API и контракт данных

```cpp
#include "Beast.h"
#include "BeastShaders.generated.h"
auto vs = BeastShaders::UIVertex();
auto fs = BeastShaders::UIFragment();
auto pso = Beast::RHI::CreatePipeline({vs, fs, Beast::RHIBlend::StraightAlpha});
auto white = Beast::RHI::CreateTexture({1, 1, {255, 255, 255, 255}, Beast::RHIFilter::Linear});
// vertices: std::array<Beast::RHIVertex, 6>, two triangles
commands.BeginRenderPass({0.1f, 0.1f, 0.1f, 1});
commands.DrawTriangles(pso, white, vertices); // optional 4th argument: RHIRect scissor
commands.EndRenderPass();
renderer->Submit(GetWindow(), commands);
```

Базовый vertex/texture ABI сохранён, reflection отклоняет неподдерживаемую схему:

| Данные | Контракт |
| --- | --- |
| RHIVertex | 32 байта: float2 position (location 0), float2 UV (1), float4 color (2) |
| VS → FS | float2 UV location 0, float4 color location 1 |
| FS output | float4 location 0 |
| Texture | Texture2D float4, set 0 / binding 0, только fragment stage |
| Sampler | SamplerState, set 0 / binding 1, только fragment stage |
| Необязательные константы FS | `BeastDraw.values[8]`: 8 float4, 128 байт, offset 0 / stride 16 |

Используйте `[[vk::location(n)]]` и `[[vk::binding(n, 0)]]` в HLSL, как в примере.
Для одноцветного рисования также используется white texture. Дополнительные
дескрипторы, массивы текстур, uniform/storage buffers и произвольные push constants
отклоняются, а не получают неверную привязку.

Для параметров примитива можно объявить в HLSL:

```hlsl
struct DrawConstants { float4 values[8]; };
[[vk::push_constant]] ConstantBuffer<DrawConstants> BeastDraw;
```

`DrawTriangles(pso, texture, vertices, scissor, constants)` копирует
`RHIDrawConstants` (array<float,32>) в команду; по умолчанию все значения нулевые.
Допускаются только конечные числа. Vulkan передаёт 128 байт push constants во FS;
OpenGL/GLES — uniform vec4[8], найденный по сгенерированному `constantsUniform`.
Произвольные имена/размеры/vertex-stage блоки отклоняются reflection.
Это не общий descriptor/uniform-buffer API. Пример использования — UIShape.hlsl.

Position — NDC [-1,1], Y вниз; глубина VS в [0,w]. Vulkan использует положительный
viewport; при генерации GL vertex shader выполняются flip Y и Z clip-space fixup.
UV (0,0) адресует верхнюю левую точку текстуры, пиксели передаются строками сверху
вниз. GLSL ES varying names унифицированы по location для линковки стадий.
Scissor/ClearRect — физические пиксели от верхнего левого угла; команды идут в
порядке записи. Отрицательные размеры отклоняются, выход за поверхность обрезается.

Текстура — immutable RGBA8 UNORM, размеры 1..4096, ровно width*height*4 байт;
дополнительно проверяется лимит GPU. Один mip, clamp-to-edge, nearest/linear.
Sampler пока часть texture description (в Vulkan это отдельный vk::Sampler).
PNG/JPEG-декодера, sRGB/color-management, update/mip generation пока нет.

Pipeline фиксирует VS/FS, triangle-list, отсутствие culling/depth/MSAA и blending:
Opaque, StraightAlpha или PremultipliedAlpha. Для последнего RGB **уже должны
быть умножены на alpha**. Формула alpha: src.a + dst.a*(1-src.a) в обоих RHI.
До 1 млн вершин на draw и до 10 млн на список; индексного Draw пока нет.

## Владение и восстановление

CreateShader/CreateTexture/CreatePipeline валидируют и копируют CPU-данные,
возвращая immutable shared handles. Это ещё не GPU allocation: нативные объекты
создаются при первом Submit, ошибки GPU/GLSL могут возникнуть именно там.
CreateShader проверяет структуру SPIR-V и entry point; полную проверку и reflection
делает offline-компилятор. API ожидает его пакет, не произвольный непроверенный SPIR-V.

DrawTriangles копирует вершины и удерживает shared handles. Закрытый список
можно отправлять повторно. Reset освобождает записи; native caches используют
weak ownership и удаляют неиспользуемые ресурсы после завершения frame fence.
Кеши принадлежат конкретному RHI, не глобальны. CPU-пакет можно сохранить при
уничтожении RHI/Android surface и использовать с новым RHI, даже другого backend.

- GL: GLSL shaders/programs, texture objects, VAO и streaming VBO; BufferData
  orphaning не затирает данные уже записанных draw. При смене EGL generation
  забываются все старые GL names, затем программа/текстуры загружаются заново.
- Vulkan-Hpp: vk::ShaderModule, vk::Pipeline/Layout, descriptor sets,
  vk::Image/Memory/View/Sampler, staging upload с layout barriers и host-coherent
  vertex buffer. Первичная загрузка immutable texture синхронна (`queue.waitIdle`),
  не на каждом кадре; streaming uploads и suballocation — следующий этап.
  Resize пересоздаёт совместимые graphics pipelines, сохраняя текстуры.

RHI и его native cache обслуживаются только потоком Run. SurfaceDestroyed
по-прежнему требует renderer.reset(); CPU handles/scene можно оставить.
Это не автоматическое восстановление Vulkan device-lost, не сериализация после
убийства процесса Android и не публичная система пользовательских render targets/fences.

## Проверки

Из корня репозитория после Debug-сборки ResourcesDemo:

```powershell
./BeastSDK/Tests/Run-RHITests.ps1
dotnet run --project Tests/ShaderCompiler.Tests
./BeastSDK/Tests/Compare-ResourceScreenshots.ps1
```

Последняя команда использует сохранённые Android screenshots; для нового прогона
сначала снимите кадры актуальных APK. Подробности результатов —
`Test/ResourcesDemo/Verification.md`. Проверка Vulkan validation выполняется на
Windows; на текущем Android-эмуляторе этот слой не установлен.

Основа offline-конвейера: [DXC SPIR-V options](https://github.com/microsoft/DirectXShaderCompiler/blob/main/docs/SPIR-V.rst),
[SPIRV-Cross CLI / reflection / clip-space](https://github.com/KhronosGroup/SPIRV-Cross/blob/main/README.md).
