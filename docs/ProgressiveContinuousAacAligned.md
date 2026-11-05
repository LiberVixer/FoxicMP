# Непрерывный remux: внутренний H.264/AAC с выровненным окончанием

5 ноября 2026. Приложение 0.2.0, публичный SDK остаётся 1.83.
[Первичные результаты и SHA](ProgressiveContinuousAacAlignedValidation-2026-11-05.json).

Добавлен частный worker mode `continuous-h264-aac-aligned-v1`: одна H.264
видеодорожка и одна AAC-LC аудиодорожка обычного MP4. Копируются параметры и
пакеты; encoder, resampler и фильтры звука в worker не используются.
**Это компонент движка, не готовая поддержка звука в continuous SDK/плеере.**
Parent/controller/cache/декодеры ещё не подключают новый mode. Capability,
`--info`, CLI/API приложений и SDK сохраняют прежние video-only профили.

## Ограничения первого компонента

- Известная длина, обычный MP4 с готовым начальным индексом, presentation start=0.
- Ровно две дорожки, H.264 и AAC-LC; их порядок в файле может различаться.
- H.264: `video_delay` 0–16; проверенный новый набор содержит B-frames.
- AAC: mono/stereo, 44,1/48 kHz, 1024 samples/frame, timebase=1/sample_rate.
- Известное окончание AAC должно быть кратно 1024 samples. На каждом пакете
  проверяются непрерывный sample clock, PTS=DTS, длительность 1024 и начальный
  skip не более 1024. Концевой discard и смена конфигурации отклоняются.
  При EOF сумма samples после начального skip должна совпасть с duration.
- Общие прежние бюджеты: input/read ≤128 MiB, output ≤64 MiB, ≤50000 пакетов
  суммарно, ≤16 MiB/packet, bounded IPC, ожидание и cancel. Лимиты не расширены.

После source probe decoder options выделяются отдельно для каждой дорожки:
FFmpeg принимает массив длиной `nb_streams`, один указатель для двух потоков
не подходит. Все options освобождаются после probe, включая отказ media.

Для нового mode применяются `delay_moov+frag_keyframe+default_base_moof`, edit
list и точная общая movie timescale (НОК знаменателей timebase, ≤INT32_MAX).
Отрицательные DTS видео и начальный AAC preroll сохраняются, независимого
обнуления дорожек нет. Основа — [документация FFmpeg MOV muxer](https://ffmpeg.org/ffmpeg-formats.html#mov_002c-mp4_002c-ismv).
Прежние параметры двух video-only режимов сохранены.

Результат частного режима дополнен video/audio packet count, initial AAC skip,
числом presentation samples и `aacEndTrimRequired=false`. Успех возможен только
после подтверждённого source EOF; `playbackSupported=false` остаётся обязательным.
Недоступные ranges удерживают тот же процесс и demux context; cancel/error не EOF.

## Почему произвольный AAC пока не допускается

Контрольный эксперимент Windows FFmpeg показал для 2,137 s AAC:
44,1 kHz — 94242 исходных PCM frames против 95232 в fragmented remux;
48 kHz — 102576 против 103424. Это лишние 990 и 848 frames соответственно.
Перезапись только `elst` duration результата проблему не устранила.
На Linux старый FFmpeg декодировал лишний хвост и у исходника, поэтому простого
сравнения двух Linux-выходов недостаточно для межплатформенного вывода.

Новый режим явно отклоняет такие невыравненные окончания до output header
(`unsupported-stream-aac-end-trim`). Успешные тесты дополнительно требуют
ожидаемое число PCM frames, а не только равенство двух потенциально ошибочных
декодирований. Фильтры обрезки к эталону/результату не применяются.
Это временная явная граница внутреннего компонента. Для обычного AAC необходимы
полный sample-window trim и отдельная приёмка parent/cache/libmpv/DirectShow;
этот пункт ТЗ остаётся открытым.

## Проверки

На GCC, MinGW, MSVC и ASan/UBSan выполнены по 17 новых сценариев:

- шесть 24 s AV fixtures: 44,1/48 kHz, mono/stereo, 25/30000/1001/60000/1001 fps,
  B-frames, в одном файле audio стоит первой; `-bf 16` — запрос энкодеру;
- полные и промежуточные payload/PTS/DTS/duration обеих дорожек, decoded video и
  **прямое побайтное сравнение PCM**, с независимым ожидаемым числом samples;
- для частичных результатов — только законченные moof/mdat до complete,
  один worker/context, несколько публикаций внутри пакета, короткие read replies;
- отказ non-aligned AAC 44,1/48 kHz, 32 kHz, другого аудиокодека;
- cancel при input wait/output ACK, integrity, unconfirmed EOF и kill при wait;
- оба прежних video-only mode по-прежнему отвергают AV source до output header.

Старый Linux ffprobe не выводил часть packet durations частичного fMP4. Тест
читает их независимо из trex/tfhd/trun и сверяет с ffprobe там, где поле есть.
Длительности не подставляются из желаемого fps. Исходный отказ oracle сохранён.

На каждой из четырёх сборок также прошли 11 прежних reordered и 9 legacy worker
сценариев. В частных копиях Linux Qt, Windows Qt и Classic с новыми worker
прошли по шесть reordered и пять default SDK playback cases. Это проверяет
совместимость video-only IPC/playback/cleanup, **не новый AAC playback**.
Linux UI/audio использовали частные X11/D-Bus/PulseAudio; Classic INI защищены
до первого запуска, shell-регистрация сверена, одноразовая задача удалена.

CTest: `progressive-stream-remux-aac-aligned`, timeout 300 s. Регистрация проверена
на обеих ОС. Для отдельного запуска нужны приватная сборка worker, FFmpeg/ffprobe
и новый каталог отчёта:

```sh
python3 tests/progressive_stream_aac_aligned_test.py \
  --worker /absolute/path/to/private-worker \
  --report-directory /absolute/path/to/new-report
```

## Следующие шаги

Sample-window trim обычного AAC, managed parent и AV cache/index, фактические
libmpv/DirectShow adapters, seek/pause/EOF/reader isolation, затем публичный SDK.
Существующие обычные/segmented AV профили этим ограничением не изменены.
Новые main EXE/публичные worker и SDK ZIP не выпускаются; установленный Linux
остаётся проверенной версией SDK 1.83. Установки Windows и профили сохранены.
Harvester не менялся. Физический A/V, оборудование, Wayland и длинный stress
не проверялись; исторические latency/resource outliers остаются открытыми.
