# R01/R04: Opus-пакеты разной длительности

15 ноября2026. База — SDK1.105, а не новый public playback mode.
Харвестер не меняется. [Фиксированный чеклист](ProgressiveOpusGeometryExecutionChecklist.md)
содержит18 подпунктов; подготовка отделена от рабочего подключения.

## Контракт следующего расширения

Текущий [derived A/V mode](ProgressiveContinuousWebmDerivedAudio.md) получает
audio count из bytes, но допускает только960samples/packet и pre-skip312.
Новый этап рассматривает variable packet duration, сохраняя VP9 SimpleBlock без
DefaultDuration, известные source length/video endpoint, mono/stereo48k,
gain0/mapping0 и отсутствие lacing. Отдельно рассматриваются pre-skip120 и312;
произвольные origins/pre-skip/mapping families этим не разрешаются.

Длительность определяется TOC и числом кадров внутри Opus packet, а не размером
пакета или его порядковым номером. [RFC6716 §3](https://www.rfc-editor.org/rfc/rfc6716#section-3)
описывает framing и предел120ms. Подготовка охватывает2.5/5/10/20/40/60/80/100/120ms
и чередующиеся20/40/60/80/100/120ms. Конечный presentation count равен сумме
длительностей минус pre-skip и конечная обрезка; правила pre-skip см.
[RFC7845 §4.2](https://www.rfc-editor.org/rfc/rfc7845#section-4.2), WebM configuration —
[Matroska A_OPUS](https://www.matroska.org/technical/codec_specs.html#a_opus).
Это ограниченный профиль, а не обещание поддержки всех разрешённых Opus форм.

Тестовый clock удерживает последний аудиопакет при подсчёте безопасного prefix.
DiscardPadding допускается только как неотрицательная конечная обрезка меньше
длительности последнего packet, с floor/ceil округлением до наносекунд. Следующий
audio packet после положительной обрезки отвергается. Обнаруженный track end
не означает writer EOF. Short zero-trim audio по-прежнему нельзя завершать по
паузе writer или по появлению более позднего video.

Начальные бюджеты прежние: source128MiB, Cluster16MiB, pending32MiB/32Clusters,
cache64MiB,50000 video+audio packets,48000000 presentation samples. Финальная
проверка сохраняет минимум два audio packets и649 presentation samples.
Сумма должна накапливаться без unsigned underflow, когда первый120-sample packet
короче pre-skip312. Временные метки сравниваются с накопленной суммой при
допуске1ms container rounding, а не с `packetIndex * 960`.

## Что именно проверяет подготовка

[Тестовый prototype](../tests/progressive_opus_geometry_prototype.py) получает
только последовательные подтверждённые chunks≤65536bytes. Известные video count
и endpoint нужны его test-only SimpleBlock converter. Три read patterns дают
одинаковый canonical output; partial packet не увеличивает count. Final count
появляется только после writer EOF. Это не production scanner/cache.

Для смешанного fixture consecutive20ms frames одного encoder объединяются
по RFC framing **до** вычисления source hashes/oracles. Границы packets нового
fixture намеренно отличаются от encoder output. После этого prototype обязан
сохранить каждый source packet и PTS/DTS; independent FFmpeg проверяет decoded
video/PCM. Repacketizer не включается в плеер и не предлагается как transcoding
или обход admission загрузчика.

Native TOC parser из рабочего кода проверяется отдельными GNU/MinGW/MSVC CLI.
Qt Player и Classic internal Matroska/Opus graph захватывают PCM завершённого
canonical file без аудиоустройства. **Эти completed-file decoder controls не
доказывают progressive ordinary-main playback нового профиля.** Существующий
20ms mode должен по-прежнему отказывать новым geometries;20ms остаётся positive
legacy control. Public SDK/capabilities не расширяются.

При подготовке обнаружено: обычный Classic Matroska graph оставляет хвост
DiscardPadding. Отчёт должен указывать точные лишние сэмплы и совпадение полезного
PCM prefix отдельно; такой результат не является точной end-trim приёмкой.
Нынешний progressive decoder adapter решает trim в своём960-sample профиле;
новому профилю также нужен собственный точный adapter, включая pre-skip120.

Видео этой подготовки — VP9 profile0/8-bit,25fps,300frames/12s. Это не новая
VFR или profile2 приёмка: их сочетание с variable Opus geometries должно пройти
production этап отдельно. Прежние SDK1.105 VFR/profile2 proofs сохраняются.

## Итог подготовки

[Машинный отчёт](ProgressiveOpusGeometryPreflight-2026-11-15.json) содержит66 строк
(11 geometries ×finite/unknown Clusters ×три редакции),198 fragmented read patterns,
66 partial-packet checks и264 clock/trim/EOF/budget refusals. Native parser:
36 positive framing controls и8 malformed controls на каждом из GNU/MinGW/MSVC;
20000 bounded real-packet mutations под ASan/UBSan/LSan, exit0.

| Проверяемый decoder path | Строк | PCM результат | Новый progressive mode |
| --- | --- | --- | --- |
| Linux Qt Player, completed file |22|Точный count и PCM относительно FFmpeg|Не подключён|
| Windows Qt Player, completed file |22|Точный count и PCM относительно FFmpeg|Не подключён|
| Classic Matroska/internal Opus graph, completed file |22|Полезный PCM prefix совпал; конечный DiscardPadding оставлен в output|Не подключён|

Шесть прежних20ms worker controls приняты,60 новых geometries штатно отвергнуты
старым mode. Шесть growing-PCM регрессий существующего Classic режима прошли.
Все окончательные compiler/harness/child exits0. Начальные отказы тестового
окружения/ожиданий и реальный Classic end-trim gap сохранены отдельно;
повторные прогоны не увеличивают число строк.703 installed files/профили и HKCU
не изменились. [Фиксированный этап](ProgressiveOpusGeometryExecutionChecklist.md):8/18,
production остаётся следующим шагом.

## Перенос в рабочий код после подготовки

| Слой | Обязательная работа |
| --- | --- |
| Profile identity | Отдельный явный opt-in mode. Не расширять молча SDK1.105 и его прежние отрицательные контроли. |
| Clock/scanner | Суммировать TOC durations и допустимый pre-skip, валидировать packet framing/clock/end-trim. Убрать деление на960 только в новом mode. |
| Sealer/worker | Удерживать audio owner до successor/EOF; terminal trim только кандидат track end. Проверять общий packet budget с video. Final counters доступны после source EOF. |
| Cache | Повторно считать durations по canonical bytes, а не верить worker result. Отдельно сверять packets/samples/pre-skip/trim/profile identity. |
| Qt | Точный skip/trim, растущий safe prefix, пауза/seek/повторное индексирование без повторного pre-skip. |
| Classic | Новая совместимая handshake/plan geometry; нынешние v2/растущий Opus допускают begin≥312 и decodeFirst%960. Не менять их ABI/admission молча. Вычислять seek/preroll по накопленным sample offsets и гарантировать end-trim. |
| API/SDK | Точные capability/validation/reason codes; динамические counters не становятся final до outputComplete. |
| Приёмка | Отдельные Linux Qt/Windows Qt/Classic ordinary-main/growing PCM, минимум10s до complete, short/zero-trim/mixed, partial packet/EOF/seek/release, resource/sanitizer и прежние profiles. |
| Поставка | Три сборки из одного commit, clean SDK и атомарный Linux upgrade только после приёмки. |

Неизвестный video endpoint/source length, one-packet audio, multichannel,
Matroska lacing, live и physical A/V стенд остаются за границей этого этапа.
Потерю целого packet с новой согласованной длиной нельзя определять только из
TOC: сохраняется требование независимой source/writer identity.

## Повторение проверки

CMake добавляет `progressive-opus-geometry-units` и три отдельные группы
`progressive-opus-geometry-short/long/mixed`. Linux запускается только через
`qt/tests/run_tests.sh`, с private X11/D-Bus/PulseAudio. Test-only CLI строится
целью `foxicmp-opus-duration-probe`, Qt PCM — `foxicmp-player-pcm-probe`.
Classic probe `tests/windows/continuous_opus_graph_probe.cpp` имеет отдельный
completed-file invocation: `probe.exe input.webm output.pcm channels`.
Старые progressive invocations сохранены; private INI содержит
ContextMenuPrompted=1/ContextMenuEnabled=0, HKCU сравнивается до/после.

Один прямой test-only запуск в подготовленном частном окружении:

```text
python3 -B tests/progressive_opus_geometry_preflight.py --worker <worker> --runner <PCM-probe> --duration-runner <duration-parser> --edition qt --report-directory <private-report> --profile mixed-mono
python3 -B tests/progressive_opus_geometry_test.py
```

Отдельный sanitizer контроль строит `tests/progressive_opus_duration_fuzz.cpp`
с `-fsanitize=address,undefined` и запускает20000 bounded mutations реальных
packets при `ASAN_OPTIONS=detect_leaks=1:halt_on_error=1` и
`UBSAN_OPTIONS=halt_on_error=1`. Аргумент — сохранённый `packets.bin` из mixed
fixture. До50000 packets/65536bytes каждый и16MiB input; успешный framing parse
мутированного пакета не выдаётся за доказательство его codec decodability.

Приложение/SDK остаются0.2.0/1.105. Только тестовые executable не требуют
переустановки плеера; INI, пользовательские файлы, Windows installations и
shell registration сохраняются. Общие77 составных требований остаются открытыми.
