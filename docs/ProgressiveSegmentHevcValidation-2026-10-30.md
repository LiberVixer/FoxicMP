# Проверка сегментного HEVC — 30 октября 2026

SDK 1.34. Испытаны Linux Qt, Windows Qt и Classic с локальными fMP4-сегментами
H.264, HEVC Main 8-bit (`hvc1`) и HEVC Main10 10-bit (`hev1`); звук — отдельный AAC-LC 48000 Гц.
Harvester и установленные Windows-приложения не менялись.

| Сценарий | Linux Qt | Windows Qt | Classic |
|---|---|---|---|
| H.264 regression, полный цикл публикации/просмотра | PASS | PASS | PASS |
| HEVC Main: просмотр до завершения загрузки | PASS | PASS | PASS |
| HEVC Main10: просмотр до завершения загрузки | PASS | PASS | PASS |
| Пропуск сегмента скрывает готовый хвост | PASS | PASS | PASS |
| Пауза при update, перемотка к 2 с и возобновление | PASS | PASS | PASS |
| Buffering на недокачанном хвосте, complete, EOF и release | PASS | PASS | PASS |

Для каждой из трёх последовательностей на каждой редакции проверены семь фаз
с одним readerId. Qt дополнительно проверяет реальные меняющиеся кадры,
`video-format=hevc`, `yuv420p`/`yuv420p10` и декодированный AAC 48000 Гц.
Это проверка software/backend воспроизведения, не GPU, HDR или физической задержки.

## Регрессия и exit codes

Native stream: 14 H.264 + 23 HEVC Main + 23 HEVC Main10 = 60 случаев на каждой ОС,
exit 0. Включены поздняя публикация, SHA-256, усечение, освобождение leases,
оба sample entry, автоматическое определение HEVC, ложный H.264 hint,
неподдерживаемый профиль и повреждённые параметры hvcC. Неполные массивы
отклоняются для hvc1, а для hev1 допускаются по его правилам.

Linux исходный CTest — 10/10 групп, exit 0. Windows исходный CTest — 9/10,
exit 8: новый H.264 test после EOF затирал уже наблюдавшийся video-format пустым
значением. После исправления capture сохранены непустые доказательства во время
playing; повтор всего runtime H.264/Main/Main10 — 5 результатов с init/cleanup,
exit 0. Native и остальные девять Windows групп прошли исходный прогон.
Ранний native test — exit 1 из-за неверного ожидания для hev1; исправлен test,
финальные 60 случаев на обеих ОС прошли.

Media regression — 468/468 на каждой ОС; single-segment diagnostic native — 18/18,
диагностический Qt IPC — 12 сценариев на каждой ОС. Public diagnostic profile
не расширялся. Python v2 SDK — 16/16; family contract — 2/2. Прежние v1 writer и
scalar HEVC Main10 проверены на обеих Qt-редакциях, exit 0. Classic v1
sessionDirectory также прошёл. Финальный Linux QTest объединяет три сегментных
профиля с двумя прежними сценариями: 7 результатов с init/cleanup, exit 0.
Windows runtime и compatibility: соответственно 5 и 4 результата, exit 0.
Все финальные обязательные exit codes перечислены в JSON; первоначальные ошибки сохранены.

## Проверенные EXE

- linuxQt: `732b640ddd4f0e6d3375ebfbc47f45bea71c49cb02af2a5e47d9e7b998608a88`
- windowsQt: `ebd6a9dd982dc4a9bff32f5dc8639ad5cdb4b973d21427c8a0ceb4a0e03a5c78`
- classic: `a22c13f3421af5fdcb61b47bc2a677fe7d2d352a742e9389cdc7d550265466b7`

## Установка и очистка

Linux EXE заменён атомарно с резервной копией `foxicmp-qt.before-sdk-1.34-20261030T043814Z`.
Сохранены INI и все 192 ранее существовавших файла.
SHA-256 INI до и после: `ee2a51243880e578a7b1f38a0278ed178d1e2e379ba0f4f756b5f77427ac24b6`.
Версия продукта остаётся 0.2.0; возможность определяется capability нового профиля.

Classic запускался отдельной копией с `ContextMenuPrompted=1`,
`ContextMenuEnabled=0`; harness сравнил HKCU commands/icons до и после.
Одноразовая задача и тестовый каталог удалены, LastTaskResult=0.
Linux использовал частные X11/D-Bus/PulseAudio; системный микшер, настройки
Windows, Wayland и физическое оборудование не затронуты.

[Машинный отчёт](ProgressiveSegmentHevcValidation-2026-10-30.json) ·
[Границы профиля](ProgressiveSegmentHevc.md).
