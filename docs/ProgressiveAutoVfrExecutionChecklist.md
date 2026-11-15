# SimpleBlock VFR: автоматический подсчёт кадров этап R01

Фиксированный этап после [SDK1.102](ProgressiveSimpleVfrProductionChecklist.md).
Всего 14 подпунктов. Общие 77 требований ТЗ считаются отдельно.

- [x] VAC-01. Зафиксировать отдельный source mode и границы без изменения старого VFR admission.
- [x] VAC-02. Подключить bounded C++ scanner с ограниченным производным счётчиком и next-PTS lookahead.
- [x] VAC-03. Подключить динамический canonical output к worker/cache без final-size oracle.
- [x] VAC-04. Согласовать mode identity, производный video count и заданный audio plan и decoder adapters Qt/Classic.
- [x] VAC-05. Добавить SDK/capabilities и отрицательные проверки контракта.
- [x] VAC-06. Проверить worker, partial structures/EOF, metadata, packets/clocks/video/PCM на Linux.
- [x] VAC-07. Проверить Windows Qt worker и PCM.
- [x] VAC-08. Проверить Classic worker и PCM с отдельным INI и неизменным HKCU.
- [x] VAC-09. Проверить ordinary Linux Qt: ≥10 s до complete, pause/seek/queued target/EOF/release.
- [x] VAC-10. Проверить те же ordinary-main сценарии Windows Qt.
- [x] VAC-11. Проверить те же ordinary-main сценарии Classic.
- [x] VAC-12. Проверить регрессии старых VFR/CFR/NTSC/25fps/AAC и sanitizer bounds.
- [x] VAC-13. Свести отчёт, обновить матрицу/план, проверить профили/установки.
- [x] VAC-14. Опубликовать исходники/clean SDK и атомарно обновить Linux с backup и неизменным INI.

Harvester не изменяется. Физический A/V стенд и широкие R01/R04 этим этапом не закрываются.


Режим без videoSamples; endpoint/audioSamples остаются известными. Старые frame-plan режимы не расширяются.

## Проверка перед поставкой

[Сводный отчёт](ProgressiveContinuousWebmOpusAutoVfrValidation-2026-11-15.json):
78 worker cases,18 ordinary-main сценариев с ≥10 s до complete,18 PCM captures,
447 прежних worker и69 main сценариев,4000 sanitizer mutations и140 SDK units прошли.
Счётчик промежуточный до EOF, затем совпадает с независимым числом кадров;
videoSamples отсутствует в новом API status и запрещён в запросе. Проверены
pause/growing seek/queued target/EOF/release и сохранность источников.

После прерывания чата сохранённые завершённые child exits использованы повторно;
незавершённые Linux сценарии повторены отдельно. Наблюдавшиеся exits и границы
восстановления записаны в отчёте; ненаблюдавшийся exit исходной оболочки не объявлен0.
691 файл установленных копий и HKCU сохранены до поставки. Физический A/V не измерен;
Classic использовал частный uncompressed null video renderer.

## Поставка завершена

Исходники и clean SDK1.103 опубликованы в `main` и `codex/linux-qt`:
[1d6ecc33b](https://github.com/LiberVixer/FoxicMP/commit/1d6ecc33b).
Linux обновлён атомарно с backup; INI сохранился побайтно. Шесть дополнительных
ordinary-main сценариев установленной копии прошли через её собственный SDK.
[Отчёт поставки](ProgressiveContinuousWebmOpusAutoVfrDelivery-2026-11-15.json).
Windows-установки и их профили сохранены; новые отдельные сборки Qt/Classic проверены.

Этот этап: всего14 / готово14 / осталось0. Основной план:77 составных требований
открыто. Неизвестные endpoint/audioSamples, другие layouts, физический A/V и
полная R01/R04/R15/R17 остаются отдельной работой. Harvester не изменён.
