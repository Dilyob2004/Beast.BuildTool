# ButtonDemo

Общий UI Beast: текст на русском, кнопка со счётчиком, переключатель доступности
и disabled-кнопка. Row/Column располагают элементы, Expanded делит ширину,
Container рисует панели с padding и скруглением. Всё рисуется C++ через RHI,
без Java UI и без Flutter.

Откройте `ButtonDemo.sln`. Код экрана — `Source/Launch/ButtonScene.h`, lifecycle
и выбор `RHIBackend::Vulkan`/`OpenGL` — `Source/Launch/Launch.cpp`.
По умолчанию в итоговой сборке выбран Vulkan. Windows-кнопки запуска и Android
launcher использует существующий генератор Beast; отдельная UI-сборка не нужна.

Из корня репозитория:

```powershell
.\BuildTool\bin\Beast.BuildTool.exe generate Test/ButtonDemo
.\BuildTool\bin\Beast.BuildTool.exe build Test/ButtonDemo --platform Windows --config Debug
.\BuildTool\bin\Beast.BuildTool.exe build Test/ButtonDemo --platform Android --config Debug
.\Test\ButtonDemo\Binaries\Windows\Debug\ButtonDemo.exe
.\BuildTool\bin\Beast.BuildTool.exe run Test/ButtonDemo --no-build --avd Medium_Phone_API_36.1 --wait
```

EXE/APK: `Binaries/Windows/Debug/ButtonDemo.exe` и
`Binaries/Android/Debug/ButtonDemo.apk`. Сохранённые варианты тестовых сборок лежат
в подкаталогах OpenGL/Vulkan; они не обновляются автоматически при обычном build.
Оба APK имеют одинаковый package id `com.beast.buttondemo`: устанавливайте их
по очереди. Пути Android SDK/NDK/Java задаются в `Config/BuildConfig.json`.

Кнопка увеличивает счётчик на отпускании и меняет соотношение верхних панелей
с 1:2 на 2:1 и обратно. Второй элемент отключает/включает её. Панель экрана
ограничена шириной 640 dp и центрируется; две кнопки делят строку поровну.
При resize ширины рассчитываются заново, не масштабируется готовая картинка.
Автоматического переноса и прокрутки при слишком маленьком окне пока нет.
На Windows работают мышь, Tab/Shift+Tab и Space/Enter; Escape закрывает окно.
На Android — касание, отмена при drag и возврат из фона без сброса дерева,
пока процесс не уничтожен ОС. Состояние на диск пока не сохраняется.

Для распространения Windows-сборки сохраните `BeastSDK-LICENSES.txt` рядом
с EXE. В Android эти же уведомления уже встроены в APK.

API/границы реализации: `BeastSDK/Source/UI/README.md`.
Результаты тестирования и изображения — `Verification.md`.
