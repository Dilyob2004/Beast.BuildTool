# TextField — Unicode, IME и accessibility

Поле на общем UI/RHI, не нативный EditText/TextBox поверх окна.
По умолчанию однострочное. Новые параметры и ограничения описаны в
[TextFeatures.md](TextFeatures.md).

```cpp
auto name = std::make_shared<Beast::UI::TextField>(
    "Эко-Герой", Beast::UI::TextFieldOptions{.placeholder = "Имя", .maxBytes = 256});
name->SetOnChanged([](const std::string& text) { /* пользователь изменил текст */ });
name->SetOnSubmitted([](const std::string& text) { /* Enter */ });
```

## Реализовано

- Общий порядок Tab/Shift+Tab для полей и кнопок; pointer focus, снятие фокуса
  при удалении из дерева/потере фокуса окна. Escape снимает фокус поля.
- Ввод через `EventType::TextInput`; стрелки, Home/End, Shift-выделение,
  Backspace/Delete, Ctrl+A, Ctrl+Z / Ctrl+Shift+Z / Ctrl+Y.
- Стрелки, Backspace/Delete и pointer hit testing учитывают extended grapheme
  clusters Unicode 17.0.0: диакритику, составные emoji, флаги, Hangul и Indic.
  Двойное/долгое нажатие использует default word boundaries UAX #29 с дополнительным
  запретом разрывать графему. Таблицы одинаковые на Windows и Android.
- Выделение мышью; touch-фокус по подтверждённому tap, без открытия редактора
  при начале жеста прокрутки. Двойное нажатие выделяет слово; долгое нажатие
  открывает меню Copy / Cut / Paste / All и маркеры выделения.
- Ctrl+C/X/V и `TextField::Edit(TextEditAction, Clipboard*)`: системный clipboard
  Windows/Android, UTF-16 → UTF-8, readonly разрешает только копирование.
  Ошибка копирования не удаляет текст при Cut. В однострочном поле вставка с переводами строк,
  неверным Unicode или превышением maxBytes отклоняется целиком.
  Clipboard читается только при явной вставке, не при отрисовке меню.
- Android: невидимый EditText служит адаптером InputConnection, само поле рисует
  Beast. Composition, предложения IME, выделение, Done/Back, UTF-16 offsets,
  защита session/revision от устаревших событий. SDK автоматически включает Java
  адаптер в обычный APK и dev host; проекту не нужен отдельный Java-код.
- IME уменьшает доступную область; ближайшие родительские ScrollView поднимают
  активное поле. Для длинной формы необходим ScrollView в дереве приложения.
- Курсор, выделение, placeholder, readOnly, горизонтальная прокрутка к каретке.
  Мигание не заставляет отправлять GPU-команды каждый кадр.
- Строгий UTF-8, байтовый лимит 1..4096, атомарный отказ неверной вставки.
  История до 64 изменений. SetText сбрасывает историю и не вызывает OnChanged.
- Смещения TextSelection — байты UTF-8 на границах Unicode scalar values,
  не UTF-16 и не индексы видимых символов. Некорректный SetSelection бросает
  invalid_argument, а не незаметно повреждает строку.

Измерение, каретка, атлас и растеризация используют общие HarfBuzz runs.
SheenBidi определяет визуальный порядок RTL/LTR; fallback выбирается по графемам.
Поддерживаются GSUB-лигатуры и GPOS-позиционирование, включая диакритику.
Каретки внутри лигатур берутся из GDEF; если их нет — advance делится между
графемами. Позиции внутри графемы для IME привязаны к началу кластера.
Shaped runs кешируются независимо от DPI: до 256 строк не длиннее 512 байт,
более длинные строки обходят кеш. Метрики и GPU-кеши остаются ограниченными.
Состояние TextField при hot reload приложение пока сохраняет явно.

## Не считать завершённой текстовой системой

Добавлены multiline, password, валидация, построчный bidi, font fallback и начальные
native accessibility providers. Это ещё не полная текстовая система: ограничения
перечислены в TextFeatures.md. Словарное выделение слов (например, тайский и китайский)
и локальные языковые правила поверх default UAX #29 пока не реализованы.
Меню действий пока имеет английские подписи, без лупы и системного ActionMode.
Windows использует существующий путь committed WM_CHAR/WM_UNICHAR, не
полноценную интеграцию Windows IME/TSF.

Сегментация следует [UAX #29 revision 47](https://www.unicode.org/reports/tr29/tr29-47.html).
IME и программный SetSelection по-прежнему принимают scalar offsets: нельзя
насильно округлять диапазон composition, присланный клавиатурой. Ручное Delete
расширяет такой частичный диапазон до графем; ReplaceSelection/вставка и IME
сохраняют точный диапазон. Удаление через Android InputConnection остаётся
операцией клавиатуры, Beast принимает её авторитетное состояние.
Сегментация сама по себе не даёт поддержки цветных emoji. Fallback включает
Devanagari, Arabic, Hebrew и Symbols2. Если подходящего глифа нет ни в одном
встроенном шрифте, используется replacement glyph, а не системный шрифт.

Следующий порядок работы:

1. Проверки IME разных производителей и Android 10 на физических устройствах.
2. Windows IME/TSF, локализация меню действий и улучшение touch selection UX.
3. Bidi-контекст всего абзаца при переносах, двойная affinity каретки и UAX #14.
4. Проверка native semantics/actions; Android AccessibilityNodeProvider/TalkBack и
   Windows UI Automation/Narrator. Проверять реальным screen reader, не только
   наличием структуры в C++. Для custom UI Android требует представления
   [виртуальной иерархии доступности](https://developer.android.com/guide/topics/ui/accessibility/views/custom-views).

## Демо и проверки

`Test/TextFieldDemo/TextFieldDemo.sln`, исходник `Source/Launch/Launch.cpp`:
имя, многострочное сообщение, RTL/LTR, password, валидация, readonly-поле,
кнопка сброса и подтверждение Enter.
Windows EXE: `Test/TextFieldDemo/Binaries/Windows/Debug/TextFieldDemo.exe`.
APK: `Test/TextFieldDemo/Binaries/Android/Debug/TextFieldDemo.apk`.
Результаты прежних сборок и проверок Gboard приведены ниже с датами;
они не подтверждают новые поля или поведение всех IME/Android 10 устройств.

Из корня репозитория:

```powershell
.\BeastSDK\Tests\Run-UnicodeTests.ps1
.\BeastSDK\Tests\Run-TextFieldTests.ps1
.\Test\EcoSbor\Tests\Run-Tests.ps1
```

Unicode-тесты проверяют все 766 GraphemeBreakTest и 1944 WordBreakTest из
Unicode 17.0.0, а также движение, удаление, историю и IME scalar positions.
Таблицы обновляются явно через Tools/GenerateUnicode.ps1; обычная сборка
не скачивает данные. Лицензия Unicode включается в уведомления EXE/APK/dev host.

Первый сценарий собирает SDK, строго компилирует изменённые UI-файлы с /W4 /WX,
проверяет UTF-8, лимиты, историю, выделение, mixed focus, callbacks с заменой
дерева, readOnly и каретку; затем реальный Windows WM_CHAR (включая surrogate
pair и Backspace) и отправку кадров TextField в OpenGL/Vulkan.
Второй — существующие UI/scroll/GPU регрессии. Оба сценария прошли 27.09.2026.

Проверки 01.10.2026: новые тесты clipboard (mock без чтения пользовательского
буфера), Ctrl+C, readonly, атомарного отказа, двойного нажатия, long press,
экранного меню и перемещения начального маркера прошли вместе с регрессиями
EcoSbor и Windows OpenGL/Vulkan. На эмуляторе API 36.1, Gboard, 1080×1800:
поле EcoSbor поднялось над клавиатурой; long press, расширение выделения конечным
маркером, Cut → Paste через реальный Android clipboard и Back прошли, PID
приложения сохранился. Проверка Android была на Vulkan dev host; физического
телефона не было. Android OpenGL/Android 10 и другие IME требуют отдельного прогона.

Проверки 02.10.2026: Unicode conformance (2710 случаев), модель редактирования,
pointer hit testing по графемам, TextField /W4 /WX, Windows OpenGL/Vulkan и все
сценарии Run-Tests.ps1 EcoSbor прошли. Debug EXE и Android APK пересобраны,
подпись APK проверена. Новая версия на Android-устройстве в этот день не запускалась.

Этап HarfBuzz (02.10.2026): проверки ffi-лигатур, идентичного покрытия é/e+acute,
GPOS mark advance, согласованности метрик/кареток и всех регрессий EcoSbor прошли.
UIGlyphAtlasTests: покрытие CPU atlas/line совпало (0/255), OpenGL readback —
максимум 1/255 на 9 DPI, Vulkan submission/recreation также прошли.
Windows EXE и hot-reload host (`--prepare-only`) собраны; Android Debug/Release
APK собраны и подписи проверены. Release APK: 5 109 558 байт, локальный debug
ключ для тестирования; для публикации требуется android.releaseSigning.
Запуск на телефоне и реальная замена модуля в этой проверке не выполнялись.
