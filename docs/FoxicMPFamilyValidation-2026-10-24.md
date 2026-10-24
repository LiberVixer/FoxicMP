# FoxicMP 0.1.73: семейная проверка

Проверка проведена 24 октября 2026 года после выравнивания общих команд и
значений новых профилей. Эталон меню и настроек — Linux Qt.

## Что изменено

- `shared/familydefaults.h`: громкость 75, mute off, сохранение позиции и
  геометрии on, ограничение пропорций окна off, OSD off, перемотка 1/5/20 с.
- `shared/familycommands.inc`: 43 стабильные Qt/Classic пары команд и
  стандартных клавиш. Qt использует их как fallback; Classic применяет общие
  Win32 key/modifier до загрузки `Commands2`.
- Сохранённые значения, включая явно пустую горячую клавишу, не заменяются.
  Платформенные APPCOMMAND, мышь, подписи, IPC и профили остаются отдельными.
- План уточнён: меню и настройки Windows Qt и Classic приводятся к Linux Qt,
  а Linux Qt не откатывается к старой структуре Classic.

## Проверки

- Linux Qt: CTest `classic-interface`, `playback-controls`, `input-control`,
  `profile-archives` — **4/4 PASS**; дополнительно общий семейный сценарий
  прошёл отдельно. Регрессия использовала частные X11/D-Bus/PulseAudio и не
  меняла рабочий микшер.
- Windows Qt: native suite — **21/21 PASS**, включая новый семейный сценарий;
  проверены новые/старые профили, дерево настроек Linux Qt, 10 категорий,
  40 страниц, поиск, сохранённые значения и пользовательские клавиши.
- Windows Classic: две изолированные копии `FoxicMP64.exe` с отдельными INI;
  новая копия подтвердила громкость 75, mute off, OSD off, RememberFilePos,
  RememberWindowPos, JumpDist 1/5/20 с и команду громкости; пользовательская
  копия сохранила volume 31, mute on, OSD on, собственные seek distances и
  shortcut. Оба процесса завершились с кодом 0.
- Установки: Linux Qt `0.1.73`, Windows Qt `0.1.73`, Classic
  `1.9.1.75 build 261024-0901`. Linux INI SHA-256 до/после:
  `a8c20483630c68962bb365b7f2abb60bf6e3d44fe28e6af9b39be7ba71fe3849`.
  Windows Qt INI до/после:
  `60988165537955DA7D11F20CFE6E2707658256322F7744830B3B68CD332D3F31`.
  Windows Classic INI до/после:
  `DF5AAE3CE2B978B45F9CA4744C0EFA13D0F80B5588B302E0E5DAF3EB84BE49D9`.
- Реальный рабочий звук не менялся: PipeWire default sink остался
  `alsa_output.pci-0000_00_05.0.analog-stereo`, `Mute: no`; временные тестовые
  sink отсутствуют.

Отложены по прежнему решению пользователя: DVD/Blu-ray интерактивные меню,
аналоговое ТВ, физические устройства и Wayland.
