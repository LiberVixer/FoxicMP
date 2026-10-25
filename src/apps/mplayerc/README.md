# FoxicMP Classic — Windows

Редакция Classic общего выпуска **FoxicMP 0.2.0**. Внутренний номер движка
сохранён: **1.9.1.75** в `include/Version.h`. Опубликованная сборка:
`261025-1303`, исходный коммит `2352824127a85d7e6eeef241c8a88701477dd850`.

## Где находится код

- `FoxicMP.vcxproj` — основное приложение MFC/DirectShow.
- `MainFrm.cpp`, `mplayerc.cpp` — окно, команды, воспроизведение и запуск.
- `PPageSheet.cpp`, `PPage*.cpp` — штатные страницы настроек.
- `SettingsTheme.cpp` — светлая/тёмная тема настроек и прокрутки.
- `ClassicProgressiveApi.cpp` — адаптер JSON API v1 через Windows named pipe.
- `../../DSUtil/HarvesterSession.cpp` — чтение источников во время скачивания.
- `../../../shared/` — общие контракты семейства; Classic сохраняет свои defaults
  и пользовательские назначения клавиш.

## Сборка и пакет

Зависимости и подготовка MSVC/MSYS описаны в
[docs/Compilation.txt](../../../docs/Compilation.txt). Из корня репозитория:

```bat
build.bat Build x64 Packages NoWait
```

Основной EXE появляется в `_bin/FoxicMP_x64/FoxicMP64.exe`. Штатный упаковщик
добавляет заводской `distrib/Defaults/FoxicMP.ini` как `FoxicMP64.ini`, языки,
значки, шейдеры, DirectX DLL и сведения об авторах. Личный INI из build-папки
использовать для публичной поставки нельзя.

## Установка и совместимость

Распакуйте portable-пакет и запустите `FoxicMP64.exe`. Для обновления сохраните
свой `FoxicMP64.ini`: профиль из архива предназначен для новой установки.
Выпуск 0.2.0 поставляется как `FoxicMPClassic-0.2.0-windows-x64.zip`; внутренний
номер Classic и имена файлов сохранены для совместимости.

Qt-редакции находятся в `qt/`, имеют отдельные EXE, INI, IPC и регистрацию.
При согласовании меню Classic сохраняет родные страницы, локализацию,
оформление и клавиши. Интеграция YouTubeHarvester в этом выпуске не менялась.

[Общий README](../../../README.md) ·
[Заметки выпуска](../../../docs/ReleaseNotes-0.2.0.md) ·
[Сравнение редакций](../../../docs/FoxicMPFamilyMatrix.md).
