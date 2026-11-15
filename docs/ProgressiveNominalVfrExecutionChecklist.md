# SimpleBlock VFR: номинальный DefaultDuration этап R01

Фиксированный этап после [SDK1.103](ProgressiveAutoVfrExecutionChecklist.md).
Всего 14 подпунктов. Общие 77 требований ТЗ считаются отдельно.

- [x] NVF-01. Зафиксировать отдельный source mode и границы без изменения старого VFR admission.
- [x] NVF-02. Подключить bounded C++ admission номинального DefaultDuration, без изменения next-PTS интервалов.
- [x] NVF-03. Подключить динамический canonical output к worker/cache без final-size oracle.
- [x] NVF-04. Согласовать mode identity, производный video count и заданный audio plan и decoder adapters Qt/Classic.
- [x] NVF-05. Добавить SDK/capabilities и отрицательные проверки контракта.
- [x] NVF-06. Проверить worker, partial structures/EOF, metadata, packets/clocks/video/PCM на Linux.
- [x] NVF-07. Проверить Windows Qt worker и PCM.
- [x] NVF-08. Проверить Classic worker и PCM с отдельным INI и неизменным HKCU.
- [x] NVF-09. Проверить ordinary Linux Qt: ≥10 s до complete, pause/seek/queued target/EOF/release.
- [x] NVF-10. Проверить те же ordinary-main сценарии Windows Qt.
- [x] NVF-11. Проверить те же ordinary-main сценарии Classic.
- [x] NVF-12. Проверить регрессии старых VFR/CFR/NTSC/25fps/AAC и sanitizer bounds.
- [x] NVF-13. Свести отчёт, обновить матрицу/план, проверить профили/установки.
- [ ] NVF-14. Опубликовать исходники/clean SDK и атомарно обновить Linux с backup и неизменным INI.

Harvester не изменяется. Физический A/V стенд и широкие R01/R04 этим этапом не закрываются.


Режим без videoSamples; endpoint/audioSamples остаются известными. Старые frame-plan режимы не расширяются.


Отдельный mode требует DefaultDuration1ms–1s как hint, но использует реальные PTS. videoSamples/videoRate запрещены; endpoint/audioSamples известны.

## Проверка перед поставкой

[Машинный отчёт](ProgressiveContinuousWebmOpusNominalVfrValidation-2026-11-15.json):
99 worker cases,24 ordinary-main cases (18 основных и6 с hint1ms/1s),18 PCM captures,
525 legacy worker cases,69 legacy main cases,142 SDK units,4000 sanitizer mutations.
Три read-budget проверки и9 дополнительных отказов некорректного DefaultDuration.
Все наблюдавшиеся exit codes равны0. Частные X11/D-Bus/PulseAudio в Linux;
Classic использует отдельный INI и private null video renderer. Физическая
синхронизация A/V и визуальный рендер Classic этой серией не измерены.
Все695 файлов установленных редакций и HKCU остались неизменными до поставки.
