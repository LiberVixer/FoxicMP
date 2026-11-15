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
- [ ] ADC-08. Перенести bounded derived-audio state machine в рабочий C++ scanner.
- [ ] ADC-09. Подключить worker/start/result без внешнего audioSamples и с точной mode identity.
- [ ] ADC-10. Подключить независимую проверку cache count/trim/coverage; отделить provisional track end от writer EOF.
- [ ] ADC-11. Подключить decoder adapters Qt/Classic, включая short audio, pause/seek и repeat indexing.
- [ ] ADC-12. Добавить совместимый SDK/capabilities и отрицательные проверки, сохранив старые known-plan modes.
- [ ] ADC-13. Проверить ordinary Linux Qt ≥10 s до complete, pause/growing/queued seek/EOF/release.
- [ ] ADC-14. Проверить те же ordinary-main сценарии Windows Qt.
- [ ] ADC-15. Проверить те же ordinary-main сценарии Classic, прежние modes и неизменность HKCU.
- [ ] ADC-16. Проверить sanitizer/resource bounds; clean SDK, три сборки и атомарное обновление Linux с backup/неизменным профилем.

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

Всего16 /готово7 /осталось9. Общих открытых составных требований ТЗ77.
