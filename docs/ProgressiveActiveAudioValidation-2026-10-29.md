# Проверка изоляции активного optional audio — 29 октября 2026

SDK 1.29: player-only writer, три редакции, отдельные тестовые профили.
YouTubeHarvester и установленные Windows-приложения не изменялись.

| Проверка | Linux Qt | Windows Qt | Windows Classic |
|---|---|---|---|
| Обрезание выбранной дорожки, переход на primary | PASS | PASS | PASS |
| Отказ optional track/resource writer | PASS | PASS | PASS |
| Изменение initialization при update | PASS | PASS | PASS |
| Обрезание невыбранной дорожки, запрет повторного выбора | PASS | PASS | PASS |
| Ошибка во время паузы, сохранение паузы | PASS | PASS | PASS |
| Ошибка required audio остаётся общей | PASS | PASS | PASS |
| Активные WebM Opus/Vorbis после ошибки | PASS | PASS | PASS |
| Субтитры, seek и естественный EOF после локальной ошибки | PASS | PASS | PASS |
| v1 indexed/fragmented, реальные Qt audio menu/cycle | PASS | PASS | API/EOF PASS |

Пять групп Qt regression на каждой ОС: 42 QtTest результата, включая init/cleanup,
exit 0. Это partial audio, active-audio errors, полная optional audio, confirmed
multi-audio seek и metadata UI. После поправки совместимости старого v1 итоговые
Qt EXE дополнительно прошли 5 результатов: v1 с реальными menu/cycle commands
и selected/unselected error paths, exit 0. В Qt наблюдались реальные AAC decoder
parameters 48 kHz после fallback, меняющиеся видеокадры и восстановленные SRT.
Отдельно проверены активные WebM Opus/Vorbis: ещё 4 QtTest результата на каждой ОС, exit 0.
Это не новое измерение точности A/V-синхронизации.

Native media/resource corpus: **468/468, exit 0**; family contract: exit 0.
Classic: восемь профилей ошибок и два v1 сценария, exit 0. Его decoder/splitter
путь и фактический AudioSwitcher выбор проверены через общий сценарий; PCM-параметры
libmpv не выдаются за измерение Classic. Команды и иконки HKCU сравнивались до/после.
Три одноразовые Classic-задачи итоговых проверок и отдельные тестовые каталоги удалены после проверки.

Ошибочная дорожка остаётся в карантине после восстановления файла и следующих
revision. Primary продолжает работу, а required ошибка закрывает сеанс. Полное
хеширование payload и универсальная изоляция ошибок backend не реализованы.
Дисковые меню, оборудование и Wayland не проверялись.

## Проверенные EXE

- linuxQt: `3f2fb2d7e60f71c1c16b307d308e0b91f4f6c9674ed75a330f046056c19b83ca`
- windowsQt: `48a8f7b9e0f51e2be48061d1803470ca2f486c050c47a6f1d298418867131b1c`
- classic: `c0e7ae98a5aaa6609b00139ef62fba617a8ba5eacdc4233c545c258f0ddaefaf`
- nativeMedia: `bd9feda6f53984d2e04520fc9683e057b0c615faf139d0cee8f924282492e8dd`

## Установка Linux

Итоговый EXE заменён атомарно, без остановки просмотра. Сохранены резервный EXE,
INI и все прежние личные файлы. Проверено 102 существующих файлов.

INI до/после: `ee2a51243880e578a7b1f38a0278ed178d1e2e379ba0f4f756b5f77427ac24b6`.

Резервная копия: `foxicmp-qt.before-sdk-1.29-20261029T175437Z`.

Полные фазовые состояния, reader identity, lifecycle доказательства и точные
exit codes: [машинный отчёт](ProgressiveActiveAudioValidation-2026-10-29.json).
Сценарий: `tests/progressive_partial_audio_conformance.py`; CTest
`progressive-active-audio`; Classic `--active-audio-only` и `--directory-only`.
Неудачные промежуточные прогоны сохранены отдельно в `_bin/active-*` и не включены
в итоговые PASS.
