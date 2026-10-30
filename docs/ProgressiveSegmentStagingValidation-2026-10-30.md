# Проверка staging и demux одного сегмента — 30 октября 2026

SDK 1.32. Harvester и установленные Windows-приложения не менялись. Это
диагностика через API, не готовое HLS/DASH воспроизведение.

| Проверка | Linux Qt | Windows Qt | Windows Classic |
|---|---|---|---|
| 12 сценариев player IPC: staging, update, cancel, timeout, integrity, release | PASS | PASS | PASS |
| Настоящие отдельные H.264/AAC fMP4: индекс и SHA init+media | PASS | PASS | PASS |
| 18 native сценариев, включая пакеты/PTS/DTS/SHA против ffprobe | PASS | PASS | Общий модуль через player API; сырые таблицы по IPC не передаются |
| Прежний resource API, validate и v1 directory contract | PASS, exit 0 | PASS, exit 0 | v1 directory contract PASS, exit 0 |
| Полный native media corpus | PASS | PASS | Новая диагностическая ветка проверена отдельно |

Новый Qt API-тест даёт 3 QtTest результата на каждой ОС, включая init/cleanup;
сохраняется тот же active playback source. Отдельная Qt-группа совместимости —
5 результатов на каждой ОС, exit 0. Native staging — 18 случаев, native
segment API и реальный IPC каждой редакции — по 12. Python SDK tests — 38/38,
family contract — 2/2, exit 0.

Windows: 8/8 CTest групп, exit 0; повтор native staging с окончательным тестом —
exit 0. Linux: исходный общий прогон дал exit 8 из-за отсутствующего поля
`duration_time` в версии ffprobe тестового окружения. Остальные 7 групп прошли;
после расчёта границы по следующему фрагменту native staging прошёл с exit 0.
Это исправление теста, не проигнорированная ошибка продукта. Исходный лог сохранён.

Classic тестировался отдельной копией с ContextMenuPrompted=1/ContextMenuEnabled=0.
Общий harness сверил команды и иконки установленного Classic в HKCU до/после.
Одноразовая задача и тестовый каталог удалены; LastTaskResult=0.

## Проверенные EXE

- Linux Qt: `14cdcde7ac454639f7b4295f8ad02e1a03f52fbb52687ac931412ed41393239e`
- Windows Qt: `e3dcf97c79efbbe7cfdd8a85aed9aa4920101cc0e54fcee95b1a1568f8e00c1b`
- Windows Classic: `d09620fb82e606fc321db9ee028132b28cdee5eb5ca3dd8ebbf9a96849dc8e5a`
- Native segment runner: `4d344423e1b6e99e5584caa3c34d365edb8291e22bafdc0b46e4a4439c5bfe2d`

## Установка и границы

Linux EXE заменён атомарно, без остановки просмотра. Профиль не изменился:
`ee2a51243880e578a7b1f38a0278ed178d1e2e379ba0f4f756b5f77427ac24b6`.
Проверена неизменность 190 существующих файлов.
Резервная копия — `foxicmp-qt.before-sdk-1.32-20261030T033627Z`.

`segmentProbeVersion=1`; `schema2Playback.segments=false`. Нужна новая сборка
Windows, чтобы интеграция могла использовать новую диагностику; SDK ZIP сам не
заменяет Windows EXE. Время пакетов проверено на исходной шкале init/tfdt/edit
list; общей presentation timeline, decoder reinit, непрерывного playback, TS,
WebM/muxed segments и live compaction ещё нет. Это не новое измерение A/V drift.

Полные ответы, фазы и точные exit codes: [JSON-отчёт](ProgressiveSegmentStagingValidation-2026-10-30.json).
