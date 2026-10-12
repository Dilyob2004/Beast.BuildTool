# Текстовые возможности и accessibility

Состояние реализации: 12.10.2026. Базовые возможности добавлены, полная
эквивалентность Flutter не заявляется.

## API

```cpp
using namespace Beast::UI;
auto message = std::make_shared<TextField>("", TextFieldOptions{
    .placeholder = "Сообщение", .maxBytes = 1024,
    .multiline = true, .minLines = 3, .maxLines = 6});
auto password = std::make_shared<TextField>("", TextFieldOptions{
    .placeholder = "Пароль", .password = true});
auto required = std::make_shared<TextField>("", TextFieldOptions{
    .placeholder = "Имя",
    .validator = [](const std::string& value) -> std::optional<std::string> {
        if (value.empty()) return "Введите имя";
        return std::nullopt;
    }});
required->SetAccessibilityLabel("Имя пользователя");
bool valid = required->Validate();
```

- `multiline`: Enter вставляет перевод строки; переносы сохраняют целые графемы,
  высота ограничена minLines/maxLines, каретка прокручивается внутри поля.
  Стрелки вверх/вниз выбирают ближайшую горизонтальную позицию соседней строки.
- `password`: один маркер на графему; Copy/Cut запрещены; значение в semantics
  замаскировано, Windows UIA Value не раскрывает пароль. Это не защищённое
  хранилище: приложение и Android IME по необходимости имеют исходную строку.
  Multiline и password одновременно запрещены.
- Validator возвращает сообщение ошибки или `nullopt`. Проверка вызывается
  при пользовательском изменении, однострочном Enter или явном `Validate()`.
  `SetText()` очищает ошибку; для проверки программного значения вызови `Validate()`.
- Лимит остаётся 4096 байт UTF-8. SetText не вызывает OnChanged.
- Android получает multiline/password input flags. В password отключено
  персонализированное обучение IME; режим полноэкранного extract UI не запрашивается.

## Отрисовка и Unicode

HarfBuzz формирует глифы и метрики, SheenBidi 3.0.0 определяет визуальные runs.
К встроенным NotoSans добавлены Devanagari, Arabic, Hebrew (Regular/Bold) и
Symbols2. Материальные иконки не используются как fallback текста.
Атлас различает реальные font IDs. Выделение mixed RTL/LTR может состоять из
нескольких прямоугольников; оно использует геометрию shaped clusters.

Ограничения, требующие следующего этапа:

- Bidi пока разрешается для каждой отображаемой строки отдельно. Контекст
  embedding/isolate всего абзаца через мягкие переносы не сохраняется.
- У логической позиции одна основная каретка, без полной двойной bidi affinity.
- Переносы: пробел/дефис/tab и аварийный перенос по графемам, не полный UAX #14.
- Цветные emoji, все письменности и системный fallback не реализованы.
- Windows использует committed WM_CHAR/WM_UNICHAR; native IME/TSF ещё нужен.

## Accessibility

SDK автоматически формирует узлы для Text, Paragraph, Button/Pressable,
TextField и ScrollView. `SetAccessibilityLabel()` задаёт явное название, в том
числе для кнопок только с иконкой. Pressable объединяет текст дочерних элементов.
Границы учитывают clipping, safe area и physical scale.

Native providers получают снимки, а не ссылки на виджеты. Действия проходят
ограниченную очередь и выполняются в UI-потоке; удалённые виджеты не вызываются.

- Windows: WM_GETOBJECT, UI Automation fragments, Invoke/Value/Scroll patterns.
  Запрос провайдера включает снимки даже без подписки клиента на события.
  Отключение окна делает сохранённые COM-провайдеры недоступными.
- Android: AccessibilityNodeProvider с виртуальными узлами, touch exploration,
  focus/click/set text/selection/scroll actions. Невидимый IME editor исключён
  из accessibility дерева.

Пока это базовый слой: нет Windows TextPattern/TextRange, полноценной иерархии
семантических групп, offscreen-навигации по всему списку и проверки всех сценариев
Narrator/TalkBack. Не считать native provider равнозначным проверенной поддержке
экранного диктора. Реальная проверка с дикторами и Android IME остаётся обязательной.

## Проверки

Из корня репозитория:

```powershell
.\BeastSDK\Tests\Run-UnicodeTests.ps1
.\BeastSDK\Tests\Run-TextFieldTests.ps1
.\Test\EcoSbor\Tests\Run-Tests.ps1
```

`UITextFeaturesTests` проверяет bidi-runs, шрифты, геометрию выделения, переносы,
CRLF, multiline, валидацию, маскирование пароля и semantic actions.
`UIAccessibilityTests` проверяет реальные COM-интерфейсы провайдера на отдельном
скрытом HWND: активацию без событий, навигацию, фокус, Invoke/Value/Scroll,
readOnly/disabled/password, удаление дерева и Detach. Это не end-to-end тест Narrator.
`UITextFieldTests` также проверяет native Windows input и кадры OpenGL/Vulkan.

Проверено 12.10.2026: Run-UnicodeTests (766 графемных + 1944 словарных случая),
Run-TextFieldTests, включая новые UIA-контракты и строгую компиляцию `/W4 /WX`,
прошли. Исправлена активация дерева по запросу провайдера без подписки на события;
NaN/Infinity в UIA ScrollPercent отклоняются и при `/fp:fast`.
BuildTool Release собран. Narrator/TalkBack и Android IME в этом прогоне не запускались.
Run-Tests EcoSbor также прошёл: вкладки, жесты, scroll physics, текстовые кеши,
fallback, сохранение hot-reload состояния и кадры OpenGL/Vulkan. Эти регрессии
не являются замером плавности на Android-устройстве.

Демонстрация новых полей: `Test/TextFieldDemo/Source/Launch/Launch.cpp`.
После изменения SDK следует пересобрать приложение; запущенный hot-reload host
нужно перезапустить с новой версией SDK.
