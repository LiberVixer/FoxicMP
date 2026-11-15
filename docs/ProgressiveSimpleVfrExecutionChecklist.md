# SimpleBlock VFR: подготовительный этап R01

Это отдельный этап после завершённого [VFR BlockDuration](ProgressiveVfrExecutionChecklist.md).
Проверяется тестовый потоковый прототип, не новый public режим плеера.
Всего **8** подпунктов; завершение меняет статус, а не добавляет доказательства
как новые выполненные требования. Основной оставшийся план — 77 требований.

- [x] SVP-01. Подготовить SimpleBlock VFR fixtures без DefaultDuration: VP9 p0/p2, fractional cadence, Opus mono/stereo, finite/unknown Clusters.
- [x] SVP-02. Реализовать тестовый incremental parser, читающий только подтверждённые bytes и соблюдающий bounds.
- [x] SVP-03. Проверить next-PTS lookahead, удержание последнего кадра и подтверждённый EOF без ложного complete.
- [x] SVP-04. Проверить перепаковку BlockDuration и удаление устаревших SeekHead/Cues/Position/PrevSize/CRC без изменения packets/clocks/trim.
- [x] SVP-05. Проверить partial headers/payloads, clock/count/EOF errors и лимиты pending buffers.
- [x] SVP-06. Проверить canonical output через существующий VFR worker и настоящие PCM-декодеры трёх редакций.
- [x] SVP-07. Свести exits, SHA, packet/PCM/video сравнения, сохранность исходников, установок и HKCU.
- [x] SVP-08. Обновить контракт следующего production-режима, воспроизводимые тесты и опубликовать результаты.

Harvester не изменяется. Новые capabilities/SDK version и новые сборки плееров
не объявляются до реализации production scanner/cache/API и ordinary-main
приёмки. Это ограничение этапа, а не отметка о готовности всего SimpleBlock VFR.

Доказательства SVP-01–07: [сводный отчёт трёх редакций](ProgressiveSimpleVfrPreflight-2026-11-15.json).
Контракт и ограничения: [план переноса в production](ProgressiveSimpleVfrRemuxPlan.md).

Исходники и результаты опубликованы атомарно в `main` и `codex/linux-qt`:
[2a7b153cf](https://github.com/LiberVixer/FoxicMP/commit/2a7b153cf).
Подготовка: всего 8 / готово 8 / осталось 0. Основной план: 77 требований открыто.
Следующая работа — production SimpleBlock mode, а не повтор этого preflight.
