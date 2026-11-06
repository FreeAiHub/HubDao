# MiniCalc

Крихітний кросплатформний калькулятор з GUI для **Windows, Linux і macOS**.
Один спільний код на C + тонкий шар під кожну ОС на її **рідному** API —
без Electron, Qt, Python, .NET, Java та інших додаткових програм.

| | Windows | Linux | macOS |
|---|---|---|---|
| GUI | Win32 + GDI | Xlib (X11 / XWayland) | Cocoa |
| Залежності | лише системні DLL | `libX11` (є в кожному десктопі) | лише системні фреймворки |
| Розмір файлу | ~55 КБ `.exe` | ~35 КБ | universal (ARM + Intel) |
| RAM | < 5 МБ (private) | ~2.6 МБ RSS / ~1 МБ PSS | див. примітку |

## Можливості
- `+ − × ÷ %`, дужки, десяткові, унарний мінус, пріоритет операцій.
- Попередній перегляд результату під час набору.
- **Історія 15 останніх обчислень** — зберігається на диску і відновлюється після перезапуску.
  Клік по запису — вставити результат у вираз. Кнопка «Очистити».
- Повна робота з клавіатури: цифри, `+ - * / % ( ) . ,`, `Enter`/`=`, `Backspace`, `Esc`/`Delete` — очистити.
- Масштабування вікна, HiDPI на Windows/macOS.

Де лежить історія:
- Windows: `%APPDATA%\MiniCalc\history.txt`
- Linux: `~/.local/share/minicalc/history.txt` (або `$XDG_DATA_HOME/minicalc/`)
- macOS: `~/Library/Application Support/MiniCalc/history.txt`

## Готові збірки
GitHub → **Actions → MiniCalc** → останній запуск → *Artifacts*:
`MiniCalc-windows-x64`, `MiniCalc-linux-x86_64`, `MiniCalc-macos-universal`.
Для тегу `minicalc-vX.Y.Z` файли автоматично додаються в **Releases**.

- **Windows:** розпакувати, запустити `MiniCalc.exe`. (SmartScreen може попередити про непідписаний файл → «Докладніше» → «Виконати все одно».)
- **Linux:** `tar xzf MiniCalc-linux-x86_64.tar.gz && ./minicalc`
- **macOS:** розпакувати, перший запуск — правий клік на `MiniCalc.app` → «Відкрити» (застосунок не нотаризований).
  Або: `xattr -cr MiniCalc.app`.

## Збірка з коду
```sh
make            # Linux (потрібен libx11-dev) або macOS (Xcode Command Line Tools)
make windows    # крос-компіляція .exe з Linux (gcc-mingw-w64)
make test       # юніт-тести ядра
```
На Windows: `build_windows.bat` (MinGW `gcc` або Visual Studio `cl`; CRT статичний, тож DLL не потрібні).

## Структура
```
src/core.c        логіка, парсер виразів, історія, розкладка, малювання
src/plat_win32.c  Windows
src/plat_x11.c    Linux / BSD
src/plat_cocoa.m  macOS
tests/            юніт-тести
```

## Примітки щодо пам'яті
- **Windows / Linux** — вкладаються в 5 МБ з запасом (CI перевіряє це автоматично).
- **macOS** — сам код застосунку займає менше 1 МБ, але будь-яка Cocoa-програма на macOS
  (навіть порожнє вікно) показує в Activity Monitor ~10–20 МБ через системні фреймворки
  AppKit. Нижче цього на macOS з рідним GUI опуститися неможливо.
- **Linux:** на системах без кириличних X11-шрифтів (`xfonts-base`) написи автоматично
  перемикаються на англійські. Чистий Wayland без XWayland не підтримується.
- **Android** — не підтримується (за задумом).
