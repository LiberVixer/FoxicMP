# Переменные Opus-пакеты: фиксированный этап R01/R04

База — приложение0.2.0 / SDK1.105. Всего18 подпунктов:8 подготовительных
и10 production/приёмочных. Список фиксирован; дополнительные прогоны не
увеличивают число выполненных требований. Общие77 составных требований ТЗ
считаются отдельно. [План и границы](ProgressiveOpusGeometryRemuxPlan.md).

- [x] OPG-01. Зафиксировать TOC/framing, packet duration, pre-skip и end-trim без внешнего audioSamples.
- [x] OPG-02. Проверить bounded incremental test-only clock: confirmed bytes, partial packets, count final только после writer EOF.
- [x] OPG-03. Подготовить реальные Opus-пакеты2.5/5/10/20/40/60/80/100/120ms, mono/stereo и finite/unknown Clusters.
- [x] OPG-04. Подготовить чередующиеся20–120ms packets одного непрерывного codec state; сверить compressed packets/PTS/DTS, decoded video/PCM до/после canonical conversion.
- [x] OPG-05. Проверить framing/clock/trim, premature EOF и sample/packet budgets; сохранить отказы старого режима.
- [x] OPG-06. Проверить production TOC parser GNU/MinGW/MSVC и bounded sanitizer mutations.
- [x] OPG-07. Измерить completed-file PCM реальных Qt/Classic decoder paths; явно сохранить расхождения и не выдавать file controls за progressive playback.
- [x] OPG-08. Опубликовать отчёт, матрицу и production план; подтвердить неизменность SDK, установок/профилей и HKCU.
- [x] OPG-09. Реализовать отдельную production clock/profile identity с длительностью из packet bytes и явным pre-skip.
- [x] OPG-10. Подключить scanner/sealer/worker, сохранив bounded audio/video lookahead и прежний960-sample mode.
- [x] OPG-11. Подключить независимый cache recount, variable PCM coverage и final-counter verification без угадывания EOF.
- [x] OPG-12. Подключить Qt/Classic adapters с точными pre-skip/end-trim и seek по сумме длительностей; сохранить старый ABI/admission.
- [x] OPG-13. Добавить совместимый API/SDK/capabilities и точные отрицательные reason codes после runtime подключения.
- [x] OPG-14. Проверить ordinary Linux Qt ≥10s до complete, pause/growing/queued seek/EOF/release и growing PCM.
- [x] OPG-15. Проверить те же сценарии Windows Qt.
- [x] OPG-16. Проверить те же сценарии Classic с private INI и HKCU before/after.
- [x] OPG-17. Проверить прежние modes, sanitizer/resource bounds и неодинаковые длительности при short/zero-trim audio.
- [ ] OPG-18. Подготовить три проверенные сборки/clean SDK и атомарно обновить Linux с backup/неизменным профилем.

## Подготовка завершена

[Отчёт](ProgressiveOpusGeometryPreflight-2026-11-15.json) фиксирует66 source rows,
198 read patterns,66 partial-packet checks,264 clock/trim/EOF/budget negative
controls,108 native framing positives/24 refusals и20000 sanitizer mutations.
60 новых layouts штатно отклонены SDK1.105, шесть20ms controls приняты.

Completed-file PCM controls:44 точных Qt окна;22 Classic окна с точным полезным
prefix и **необрезанным DiscardPadding**, с явным числом лишних сэмплов. Это
зафиксированное ограничение обычного Classic graph, не точная end-trim приёмка
нового progressive mode. Шесть прежних growing-Opus сценариев Classic повторно
прошли с точным count/PCM. Все итоговые harness/child/build exits0; начальные
неудачные проверки и причины сохранены в отчёте.

Установленные703 файла/профили, SDK1.105 и HKCU сохранены. Product code/capabilities
не расширены: изменены test-only инструменты, CMake tests и документация.

**Всего 18 / готово 8 / осталось 10.** Production OPG-09–18 открыты. Предыдущий этап
[ADC16/16](ProgressiveAudioCountExecutionChecklist.md) сохраняется выполненным.
Харвестер не меняется. Физическая A/V-проверка и общие R01/R04/R15/R17 не закрываются.


## Production-подключение SDK1.106,16 ноября2026

[Рабочий контракт](ProgressiveContinuousWebmPacketOpus.md) подключает packet-derived
clock/scanner, независимый cache recount/pre-skip verification, новые Opus PCM
handshake/offsets Classic и microsecond audio view/sample seek trim Qt. Старые
режимы остаются отдельными. Worker/decoder и SDK controls прошли; выполняется
окончательная ordinary-main/PCM/compatibility матрица. Это ещё не итоговая поставка.

Промежуточный статус:18 всего/13 готово/5 осталось (OPG-14–18). Историческая
подготовка8/18 выше не является новым счётчиком. Итоговые результаты будут
сведены в [отчёт](ProgressiveContinuousWebmPacketOpusValidation-2026-11-16.json).


## Итоговая приёмка,17 ноября2026

[Отчёт](ProgressiveContinuousWebmPacketOpusValidation-2026-11-16.json) сводит
108 ordinary-main,108 complete PCM,90 growing PCM и36 sample-seek сценариев
трёх редакций.630 worker controls и741 прежний worker control,99 прежних main
сценариев завершились ожидаемыми результатами. Каждая редакция проверена отдельно;
Classic использует private null video renderer, Linux — частные X11/D-Bus/PulseAudio.
Физическая A/V-синхронизация этим не измеряется.

Найденный Linux cancel-after-close use-after-free устранён: opaque cookie IDs
не переиспользуются, callback удерживает shared ownership до возврата. Проверены
20000 close/cancel гонок под ASan/UBSan/LSan, повторная ordinary-main матрица и
continuous-player lifecycle. Исходные timeout/crash сохранены в отчёте.

**Всего18 / готово17 / осталось1: OPG-18 — clean SDK и проверенная установка.**
Общие77 составных требований остаются открытыми; это другой уровень чеклиста.
