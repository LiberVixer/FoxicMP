# SimpleBlock VFR: production этап R01

Фиксированный этап после [подготовки](ProgressiveSimpleVfrExecutionChecklist.md).
Всего 14 подпунктов. Общие 77 требований ТЗ считаются отдельно.

- [x] SVC-01. Зафиксировать отдельный source mode и границы без изменения старого VFR admission.
- [x] SVC-02. Подключить bounded C++ incremental scanner и next-PTS lookahead.
- [x] SVC-03. Подключить динамический canonical output к worker/cache без final-size oracle.
- [x] SVC-04. Согласовать mode identity, frame/audio plan и decoder adapters Qt/Classic.
- [x] SVC-05. Добавить SDK/capabilities и отрицательные проверки контракта.
- [x] SVC-06. Проверить worker, partial structures/EOF, metadata, packets/clocks/video/PCM на Linux.
- [x] SVC-07. Проверить Windows Qt worker и PCM.
- [x] SVC-08. Проверить Classic worker и PCM с отдельным INI и неизменным HKCU.
- [x] SVC-09. Проверить ordinary Linux Qt: ≥10 s до complete, pause/seek/queued target/EOF/release.
- [x] SVC-10. Проверить те же ordinary-main сценарии Windows Qt.
- [x] SVC-11. Проверить те же ordinary-main сценарии Classic.
- [x] SVC-12. Проверить регрессии старых VFR/CFR/NTSC/25fps/AAC и sanitizer bounds.
- [x] SVC-13. Свести отчёт, обновить матрицу/план, проверить профили/установки.
- [ ] SVC-14. Опубликовать исходники/clean SDK и атомарно обновить Linux с backup и неизменным INI.

Harvester не изменяется. Физический A/V стенд и широкие R01/R04 этим этапом не закрываются.

## Приёмка до установки

[Отчёт трёх редакций](ProgressiveContinuousWebmOpusSimpleVfrValidation-2026-11-15.json):
75 worker cases, 18 ordinary-main rows (≥10 s до complete), 18 PCM captures,
372 прежних worker и60 прежних main cases, 4000 sanitizer mutations и137 SDK units.
Точные exits итоговых прогонов —0; ожидаемые ненулевые exits negative controls
сохранены отдельно. Read-budget regression: old8442/new16 source calls;
DefaultDuration0 также отклонён во всех трёх worker сборках.

684 файла установленных копий и HKCU сохранены до поставки. В Classic использован
private Null Video Renderer (Uncompressed); физический A/V не измерялся.
Подпункт SVC-14 завершается после clean SDK, push и фактического обновления Linux.
