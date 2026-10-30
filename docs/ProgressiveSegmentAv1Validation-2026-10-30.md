# Проверка сегментного AV1 — 30 октября 2026

SDK 1.35. Проверены Linux Qt, Windows Qt и Classic: AV1 Main 8/10-bit в fMP4
с отдельным AAC-LC 48000 Гц. В том же прогоне сохранена регрессия H.264 и
HEVC Main/Main10. Harvester и установленные Windows-приложения не менялись.

| Сценарий | Linux Qt | Windows Qt | Classic |
|---|---|---|---|
| AV1 Main 8-bit: просмотр до окончания загрузки | PASS | PASS | PASS |
| AV1 Main 10-bit: просмотр до окончания загрузки | PASS | PASS | PASS |
| H.264, HEVC Main/Main10: прежние сегментные профили | PASS | PASS | PASS |
| Пропуск скрывает готовый хвост, заполнение запускает просмотр | PASS | PASS | PASS |
| Пауза при update, перемотка к 2 с и возобновление | PASS | PASS | PASS |
| Buffering на недокачанном хвосте, complete, EOF, release | PASS | PASS | PASS |

Каждый из пяти профилей прошёл семь фаз с одним readerId в каждой редакции.
Обе Qt подтвердили реальные меняющиеся кадры AV1, `yuv420p`/`yuv420p10`,
декодированный AAC 48000 Гц; HEVC Main10 также сохранил 10-bit. Classic читает
обе дорожки через DirectShow и достигает естественного завершения после complete.
Это не измерение физической аудиозадержки, HDR или GPU.

## Проверки и точные exit codes

Native stream на каждой ОС: H.264 — 14, HEVC Main/Main10 — по 23, AV1 8/10-bit —
по 33; всего 126, exit 0. Проверены bytes/ranges, позднее добавление сегментов,
постоянство конфигурации, SHA-256, усечение, отмена и последний владелец leases.
AV1 дополнительно проверяет автоматическое определение из init, ложные H.264/HEVC
hints, marker/version/profile/level, 12-bit/monochrome/chroma, reserved bits,
presentation delay, bounded OBU и обязательный sequence header.

Linux CTest — 9/9 групп, exit 0; native stream проверен отдельно. Windows CTest —
10/10, exit 0, включая native stream. Media regression — 468/468 на каждой ОС;
прежняя single-segment диагностика — 18 native и 12 Qt IPC случаев на каждой ОС.
Её публичный H.264/AAC-профиль не расширялся.

Runtime Qt — по 7 результатов с init/cleanup (пять media-профилей), exit 0.
Прежние v1 writer и scalar AV1 Main 10-bit — по 4 результата с init/cleanup,
exit 0 на каждой ОС. Classic v1 sessionDirectory также прошёл. Python v2 SDK —
17/17, family contract — 2/2, exit 0. Все целевые прогоны завершились успешно
с первого запуска; обязательные exit codes сохранены в JSON.

## Проверенные EXE

- linuxQt: `4eee103019489bcadca8ad06c66b410e3bf79cfb3cfb0823a3cb22df34b81f15`
- windowsQt: `fc92a51c4f63e8cecf083da109f06bf844a681fffd04d89b34788e2c19ba6733`
- classic: `5855246d689341478585193851fabb3428433535a85d9aa2d30163dc48c4e385`

## Установка и очистка

Linux EXE заменён атомарно, прежний сохранён как `foxicmp-qt.before-sdk-1.35-20261030T050615Z`.
INI и все 193 ранее существовавших файла сохранены.
SHA-256 INI до/после: `ee2a51243880e578a7b1f38a0278ed178d1e2e379ba0f4f756b5f77427ac24b6`.
Версия продукта остаётся 0.2.0; поддержка определяется capability профиля.

Classic использовал отдельную копию с `ContextMenuPrompted=1` и
`ContextMenuEnabled=0`; harness сравнил HKCU commands/icons до и после.
Одноразовая задача и тестовый каталог удалены, LastTaskResult=0. Linux тестировался
с частными X11/D-Bus/PulseAudio. Системный микшер, профили Windows, Wayland
и физическое оборудование не затронуты.

[Машинный отчёт](ProgressiveSegmentAv1Validation-2026-10-30.json) ·
[Контракт профиля](ProgressiveSegmentAv1.md).
