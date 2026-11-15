# R01: подготовка continuous WebM VP9/Opus с VFR

Срез 15 ноября 2026, приложение **0.2.0**, SDK **1.100 сохраняется**.
Это воспроизводимые fixtures, проверка native ResourceIO/index и проект нового
режима. Public VFR playback пока не реализован и не объявлен.
Harvester, установленные плееры, профили и регистрация не изменены.

## Измеренные границы

[Отчёт трёх стендов](ProgressiveWebmVfrPreflight-2026-11-15.json) содержит
54 строки: три 24-секундных VP9/Opus fixtures × три формы длительностей ×
finite/unknown Clusters × Linux Qt/Windows Qt/Classic native runners.
VP9 p0/8-bit и p2/10-bit с настоящими indexed superframes, Opus 48k mono/stereo
20ms; базовые частоты 50 и 60000/1001, интервалы меняются на 8-й и 16-й секунде.

Формы: выход FFmpeg с SimpleBlock и DefaultDuration; тестовый эквивалент с
video BlockGroup/BlockDuration; тот же эквивалент без DefaultDuration.
Преобразование fixture сохраняет compressed payload, PTS/DTS и Opus trim;
SeekHead удаляется, поскольку offsets после преобразования меняются.
Это тестовый преобразователь полного файла, не производственный growing parser.

- Для форм с DefaultDuration выполнены 72 native full/prefix projections:
  точные packets/clocks, декодированные кадры и PCM, incomplete read возвращает
  ожидание, владельцы readers освобождаются, CLI exit=0.
- Без DefaultDuration получены 36 ожидаемых initialization refusals. Это
  **открытая граница текущего native index**, а не PASS поддержки такого файла.
- Все 162 проверки прежних 25/NTSC/CFR modes отказали VFR; новая возможность
  в capabilities не добавлялась. Один существующий worker-путь возвращает
  сырой текст `WebM video requires a bounded DefaultDuration` вместо стабильного
  reasonCode. Он сохранён в отчёте и требует нормализации при новом режиме.
- Ещё 36 CSV-проверок индекса сравнили каждый packet SHA/PTS. У SimpleBlock
  индекс применяет DefaultDuration: промежутки между sample intervals доходят
  до **600000 ticks = 60ms**. С явными BlockDuration промежутки равны нулю.
  Исходная временная пауза не должна автоматически означать отсутствующие bytes.
- В первоначальном oracle FFmpeg без passthrough дал 534 decoded frames при
  733 packet frames. После `-fps_mode passthrough` получено ровно 733, hashes
  совпали с оригиналом. Первый отказ сохранён отдельно; это исправление теста.

[Matroska определяет BlockDuration](https://www.matroska.org/technical/elements.html#BlockDuration)
как длительность блока в Track Ticks. Если нет ни этого поля, ни DefaultDuration,
она определяется по следующему блоку в порядке показа. Для последнего кадра
BlockDuration помогает явно задать конец. Поэтому нельзя вычислять VFR frame
count из одной номинальной частоты либо выдавать следующий PTS до его получения.

Native CLI/index projections и FFmpeg file decode не являются обычным libmpv /
DirectShow playback или измерением физической A/V синхронизации. Положительные
проекции относятся к структурно закрытым Clusters: границы вычисляет oracle
полного fixture. Настоящий partial producer/scanner ещё предстоит реализовать.

## Воспроизведение проверки

Нужны FFmpeg/ffprobe с libvpx-vp9 и libopus и тестовые native tools из этого дерева.
CMake-тест `progressive-webm-vfr-preflight` создаёт fixtures и проверяет worker /
native projections; `progressive-webm-vfr-index` использует сохранённые views.
Запускать их вместе через `ctest -R '^progressive-webm-vfr' --output-on-failure`
из настроенной сборки. Это headless native/file тесты, аудиоустройства и GUI
не открываются. CLI каждого теста также принимает явные пути runner/report.
JSON связывает source SHA, runner SHA, точные exits и первоначальные отказы.

## Следующий отдельный профиль — пока только проект

Предлагаемое имя `continuous-webm-vp9-opus-vfr-v1`; это **не текущий API**.
Существующие modes и настройки сохраняются. Первая реализация ограничивается:

1. Один shared local WebM, video VP9 p0/p2 и обязательный Opus из SDK 1.100,
   TimestampScale 1ms, нулевое начало видео, PTS=DTS, известная duration.
2. Каждый видеопакет имеет video BlockGroup с обязательным bounded BlockDuration;
   допускается отсутствие DefaultDuration. ReferenceBlock/key status проверяются
   по VP9, codec-state/config changes и lacing пока отклоняются.
3. В start нужны независимые immutable `videoSamples` и `audioSamples` в виде
   канонических десятичных строк. `videoRate` в этом режиме отсутствует.
   Count не выводится из duration; повторный requestId обязан сохранять identity.
4. Проверяются строго возрастающие PTS, первый keyframe, положительная bounded
   duration каждого кадра, совпадение следующего PTS с предыдущим endpoint и
   точный последний endpoint с manifest duration. Потеря/добавление последнего
   packet, overlap, regression, overflow и неверные counts дают явный отказ.
5. Output cache/index должен учитывать фактические sample durations, включая
   mode-scoped допуск отсутствующего DefaultDuration только при доказанных
   BlockDuration. CFR frame-grid override здесь не применяется.
6. Сохраняются bounds 128MiB source /64MiB cache /16MiB Cluster, максимум 50000
   video+audio packets, reader ownership, SHA-проверки и структурное закрытие
   unknown Clusters. Ни ticks, ни compressed bytes не переписываются.
7. До объявленного capability — реальные обычные main трёх редакций с ≥10s
   до complete, точный PCM в обоих декодерах, pause/growing/queued seek/EOF,
   release/forget, отрицательные partial/fault rows и прежние CFR/AAC регрессии.

SimpleBlock VFR без явных длительностей — отдельный последующий этап: потребуется
удержание неразрешённого последнего кадра до next PTS/проверенного final endpoint.
Другие rates/layouts Opus, one-packet trim, offsets, discontinuities и полные
R01/R04/R15/R17 остаются открытыми. Wayland и оборудование не проверяются.

В чеклисте по-прежнему **149 выполнено, 77 открыто**: подготовка не закрывает
пункт реализации VFR. Изменены только тесты и документация; новая сборка и
переустановка не требуются.
