# Обычный M4A/audio-only

SDK1.107. Общая реализация трёх редакций, без изменений YouTubeHarvester.
Приёмка: `ProgressiveM4AValidation-2026-11-18.json`.

## Контракт

Schema2, один обязательный `audio` track, один scalar resource:
`containerHint: "m4a"`, `codecHint: "aac-lc"` или `"he-aac"`.
Capabilities: `schema2Playback.standalone.m4a`, mode
`standalone-indexed-m4a-aac-v1`, profile `standalone-indexed-m4a-aac`.
Расширение физического `.part` файла не определяет формат.

Поддержаны обычные MP4 sample tables с одной AAC-дорожкой: AAC-LC, SBR/HE-AAC
и PS/HE-AAC v2; 44100/48000 Гц, один или два выходных канала;
1024/2048 декодированных сэмпла на пакет. ASC проверяется по байтам.
Для mono SBR без явного PS=false декодер может выбрать два выходных канала;
число каналов в плане соответствует этому поведению.

Нужны известная неизменная длина resource, конечная duration в 100ns,
нулевой timestampOffset и SHA-256 подтверждённых ranges. Допустимы head-moov
или отдельный подтверждённый tail-moov. Наличие физического хвоста без ranges
не разрешает чтение индекса. До подтверждения метаданных: `waiting-index`.

Ограничения: один mdat; до 1 MiB метаданных, 250000 пакетов, 16 MiB новых
подтверждённых байтов за публикацию, prefix и не более одного metadata range;
до часа звука, до 16384 начальных сэмплов задержки. Разрешён один unit-rate
version0 edit; последовательный AAC clock и укороченный последний duration.
Зашифрованные/внешние ссылки, ctts, смена конфигурации и несколько дорожек
не входят в этот профиль. HE-AAC с нестандартной непоследовательной stts
не объявляется поддержанным по одному только имени кодека.

## Доступ и декодирование

Все чтения физического файла проходят ResourceSource и SHA-256. Индексатор
публикует только целые подтверждённые пакеты, сохраняя compressed bytes и
PTS/DTS. Видеодорожка не создаётся, оригинал не переписывается.
Для Classic строится одно аудиопредставление fMP4; для Qt дополнительно
проецируются обычные sample tables декодера. Это метаданные в памяти,
без перекодирования и промежуточной медиакопии на диске.

`m4aWindow` содержит sampleRate/channels/frameSamples/initialSkip,
точное число presentation samples и coded packets. Classic использует
отдельный IProgressiveM4AAacControl; прежний AAC-LC v1 не расширен.
Qt применяет проверенный PCM interval до пользовательских фильтров.
Обе Qt-редакции отключают повторную обработку edit list в libavformat и
переносят coded PTS в presentation clock перед PCM trim. Это проверяется
фактическим PCM каждой платформы. При смене seek target во время загрузки
сохраняется одна загрузка и обновляется начало её проверенного PCM interval.

Полное декодирование сравнивается с независимым PCM и точным sample count.
При seek AAC PNS/SBR/PS может менять фазу шумового синтеза относительно
линейного декодирования. Для seek проверяются точный sample count,
SNR не менее 60 dB и пиковая погрешность не более 0.0625 полного диапазона;
это не утверждение о побитном равенстве PCM после seek.

## Вызов из внешней программы

Используется существующий `progressive_v2.V2PreviewSession`. Он проверяет
новую capability до команды start. Данные записываются и flush выполняется
до публикации ranges; `protect_snapshot(..., previous=...)` сохраняет
неизменность старых SHA-блоков. Пока writer не complete, вызывайте heartbeat.
`close(timeout=2)` и подтверждённый filesClosed/released остаются условием
удаления временного файла. Старые manifests и команды не меняются.

## Воспроизводимая проверка

`tests/progressive_m4a_conformance.py --prepare DIR` создаёт синтетический
корпус: FDK-AAC LC/SBR/PS, FFmpeg AAC, head/tail, short/priming/end padding.
Тестовый encoder: `tests/m4a_fixture_encoder.cpp`, внешний FDK-AAC v2.0.3,
`FOXICMP_TEST_AAC_ENCODER=/absolute/path/to/encoder`. Encoder не входит
в приложение или SDK. В корпусе нет скачанной музыки и данных Harvester.

Один подготовленный корпус проверяется GNU/MSVC/MinGW:
`progressive_m4a_conformance.py --runner RUNNER --corpus DIR`.
`progressive_m4a_pcm_test.py` проверяет реальные Qt Player и Classic graph;
`progressive_m4a_controls_test.py` — Qt Window pause/seek/reload;
`progressive_m4a_api_test.py` — обычные main и ≥10 секунд до complete.
В CMake можно задать `FOXICMP_TEST_M4A_CORPUS`; Linux UI/audio тесты запускайте
через `qt/tests/run_tests.sh` с частными X11/D-Bus/PulseAudio.

Общий MP4 audio-only с Opus/FLAC/MP3/AC-3/E-AC-3 остаётся отдельной строкой M02.
Готовность M4A AAC не закрывает эти кодеки или весь контейнерный раздел ТЗ.
