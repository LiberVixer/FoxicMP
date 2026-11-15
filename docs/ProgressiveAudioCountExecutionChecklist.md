# WebM/Opus без внешнего audioSamples: фиксированный этап R01

База — [SDK1.104](ProgressiveContinuousWebmOpusNominalVfr.md). Всего16 подпунктов:
семь подготовительных и девять production/приёмочных. Новые доказательства
не добавляют пункты к этому списку. Общие77 составных требований считаются отдельно.

- [x] ADC-01. Зафиксировать семантику EOF, pre-skip/end-trim и границу потери целого пакета.
- [x] ADC-02. Проверить bounded incremental prototype без внешнего audioSamples.
- [x] ADC-03. Проверить partial packet, premature EOF, clocks/trim/framing и budgets.
- [x] ADC-04. Сверить derived count с независимым decoded PCM для full/short/zero-trim/minimal/VFR fixtures.
- [x] ADC-05. Проверить canonical output через существующие worker/PCM adapters трёх редакций, явно передавая известный count только после prototype EOF.
- [x] ADC-06. Подтвердить неизменность SDK/capabilities, установок/профилей и HKCU; не объявлять preflight новым playback mode.
- [x] ADC-07. Опубликовать воспроизводимые тесты, отчёт и production план.
- [x] ADC-08. Перенести bounded derived-audio state machine в рабочий C++ scanner.
- [x] ADC-09. Подключить worker/start/result без внешнего audioSamples и с точной mode identity.
- [x] ADC-10. Подключить независимую проверку cache count/trim/coverage; отделить provisional track end от writer EOF.
- [x] ADC-11. Подключить decoder adapters Qt/Classic, включая short audio, pause/seek и repeat indexing.
- [x] ADC-12. Добавить совместимый SDK/capabilities и отрицательные проверки, сохранив старые known-plan modes.
- [x] ADC-13. Проверить ordinary Linux Qt ≥10 s до complete, pause/growing/queued seek/EOF/release.
- [x] ADC-14. Проверить те же ordinary-main сценарии Windows Qt.
- [x] ADC-15. Проверить те же ordinary-main сценарии Classic, прежние modes и неизменность HKCU.
- [x] ADC-16. Проверить sanitizer/resource bounds; clean SDK, три сборки и атомарное обновление Linux с backup/неизменным профилем.

Harvester не меняется. Известные source length/video endpoint пока сохраняются.
Физический A/V стенд, неизвестный общий endpoint и полная R01 этим не закрываются.

## Подготовка завершена

[Отчёт](ProgressiveAudioCountPreflight-2026-11-15.json):30 source rows,
90 fragmented read patterns,30 partial-packet checks,30 existing worker и30
known-plan PCM consumers. Missing audioSamples штатно отклонён30 раз.
36 отрицательных контролей,3 semantic-EOF controls и45 unit/real-fixture limit
проверок прошли. Все финальные harness/child/unit exit codes равны0.

Установленные699 файлов, INI и HKCU не изменены; используются проверенные сборки
SDK1.104. Приложение/SDK не пересобирались. Это подготовка: новый unknown-count
ordinary playback пока не реализован. [Следующий перенос](ProgressiveAudioCountRemuxPlan.md#перенос-в-рабочий-код)
начинается с ADC-08 и должен охватить cache/PCM end вместе со scanner.

Исторический итог подготовки:16 всего /7 готово /9 осталось.
Текущая production-проверка приведена ниже; база списка не меняется.

## Production SDK1.105

[Рабочий контракт](ProgressiveContinuousWebmDerivedAudio.md) подключён к scanner,
worker, независимому cache recount и Qt/Classic PCM adapters. Новый mode не принимает
внешних audioSamples/videoSamples; endpoint/длина известны. [Отчёт приёмки](ProgressiveContinuousWebmDerivedAudioValidation-2026-11-15.json)
содержит117 worker cases,30 ordinary main,30 complete PCM и18 growing PCM captures,
624 legacy worker и69 legacy main. Отдельные cache mismatch и short zero-trim
waiting/pending-budget controls,4000 sanitizer mutations и145 SDK units прошли.
Все итоговые child exits сохранены; прерванный SSH-транспорт не выдан за успешный
remote batch. Начальные неудачные прогоны и их причины сохранены в отчёте.

Исторический gate перед поставкой:15/16. Результат финальной поставки приведён ниже.

## Поставка завершена

[Отчёт поставки](ProgressiveContinuousWebmDerivedAudioDelivery-2026-11-15.json):
clean SDK1.105, атомарное обновление Linux с backup и неизменным INI/личными
файлами; ещё10 ordinary-main сценариев с собственным установленным SDK прошли.
Windows Qt/Classic проверены отдельными сборками, установленные Windows EXE/INI
и HKCU сохранены. В Classic дополнительно пересобраны обе точки выделения
декодера (FGManager и PPageInternalFilters) и повторены10 новых/23 прежних main
сценария. Отчёт хранит hash окончательного Classic EXE.

Всего16 /готово16 /осталось0. Общие77 составных требований остаются открытыми;
этот профиль не закрывает полностью R01/R04/R15/R17. Харвестер не изменён.
