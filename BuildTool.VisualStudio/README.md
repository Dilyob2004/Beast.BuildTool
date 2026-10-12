# Beast BuildTool Launchers

VSIX 1.2.0 для Visual Studio Community 2026 (18.x). Сохраняет identity
`Beast.BuildTool.AndroidLauncher`, обновляя установленную версию 1.0.0.

- Существующий MEF Android Emulator debugger target сохранён.
- Windows (Beast) — default launch profile генерируемых проектов: читает
  `hotReload` из Config/BuildConfig.json на каждом запуске. Debug+true — watch
  без debugger, false/отсутствие/Release — обычный native EXE. Build не запускает
  долгоживущих процессов. Старый Windows Local Debugger по-прежнему доступен.
- AsyncPackage добавляет `Beast: Hot Reload (Windows, Debug)` в контекстное меню
  проекта приложения. У других проектов/BeastSDK/множественного выделения скрыт.
- Контекстный пункт запускает выбранный проект, а не startup project. Профиль
  Windows (Beast) используется обычной кнопкой запуска/F5 и выбирает режим по конфигу.
- Watch работает в отдельной PowerShell-консоли; ошибка не закрывает консоль.
  Аргументы передаются как литералы в EncodedCommand, без cmd.exe-конкатенации.
  Это запуск dev host, не native-отладка DLL внутри него.
- Package registration и таблица меню генерируются официальными CreatePkgDef/VSCT.

## Сборка

В Developer PowerShell Visual Studio:

```powershell
msbuild BuildTool.VisualStudio/Beast.AndroidLauncher/Beast.AndroidLauncher.csproj /restore /p:Configuration=Release
dotnet run --project BuildTool.VisualStudio/Tests/LaunchTests.csproj
```

Используется Microsoft.VSSDK.BuildTools 18.5.40034 из NuGet, References берутся из
установленной VS. При другом каталоге передайте `/p:VsInstallRoot=...`.
Выход: `Beast.AndroidLauncher/bin/Beast.AndroidLauncher.vsix`.
Установка стандартным VSIXInstaller при закрытой VS, без ручной правки реестра.

## Проверено 27.09.2026

Компиляция VSCT/C# и упаковка без предупреждений. Тесты выбранного пути,
пробелов/кавычек/метасимволов, dotnet-host, отсутствующих метаданных, блокировки
watch и состава VSIX прошли. Embedded Menus.ctmenu и pkgdef проверены.
Штатный установщик завершился с кодом 0, версия 1.1.0 включена; SHA256 установленной
DLL совпал со сборкой. Отображение/клик меню внутри вновь открытой VS ещё
нужно подтвердить в IDE; тесты метаданных не заменяют эту ручную проверку.

Основа меню: [Microsoft — VSCT](https://learn.microsoft.com/en-us/visualstudio/extensibility/internals/authoring-dot-vsct-files?view=visualstudio).

## Проверки 1.2.0 (28.09.2026)

BuildTool и VSIX собираются без предупреждений. LaunchTests проверяет true/false,
отсутствующее свойство, Debug/Release, повторное чтение изменённого файла,
комментарии/trailing commas и отказ неверного конфига. Новый LaunchConfigProbe
подтвердил создание hotReload=false и default DebuggerFlavor=BeastWindows.
Фактический F5 в IDE требует пользовательской проверки после установки.
