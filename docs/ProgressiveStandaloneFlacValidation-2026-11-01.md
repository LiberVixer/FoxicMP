# Проверки SDK 1.62: FLAC-аудио в растущем fMP4

1 ноября 2026, приложение 0.2.0. [Контракт](ProgressiveStandaloneFlac.md),
[машинный отчёт](ProgressiveStandaloneFlacValidation-2026-11-01.json).
Harvester не изменялся. Raw `.flac` этим этапом не добавлен.

- Linux Qt, Windows Qt и Windows Classic: восемь реальных 20-секундных
  источников — все сочетания mono/stereo × 16/24 bit × 44100/48000 Hz.
  Начальная длина mono неизвестна. Проверены waiting-init, недостаточный буфер,
  playing после подтверждённых 12 s, pause, seek 2 s в паузе, resume, tail wait,
  complete, seek 17 s, natural EOF и release. ReaderId сохраняется;
  сжатый ресурс после теста идентичен оригиналу.
- Qt проверяет фактический FLAC decoder, sample rate и channels, один audio
  track без video. Classic использует штатный AP4_FLACSampleEntry, MP4 splitter,
  FLAC decoder/DirectShow graph и native pause/chapter commands. Физический
  аудиовыход и его побитная точность не аттестуются.
- Native Linux и Windows: по 71 проверке. Для восьми профилей — один Track и
  resource, kind-aware index и полное число packets, точные decoder bytes,
  отсутствие EOF до complete, last-owner release. PCM, декодированный из
  bytes после bridge, совпадает по длине/SHA-256 с независимым исходным
  целочисленным PCM. Для 24-bit сравнение выполнено в точном s32le представлении.
- Отказы без readers: codecHint/actual mismatch, пустой hint, неподдержанный
  container/kind/optional shape, SHA/prefix/offset; dfLa version/flags/last-bit,
  тип/размер metadata, block/frame bounds, rate/channels/depth, расхождения
  sample entry/STREAMINFO/media clock, отсутствующий/дублирующий/не первый dfLa,
  усечённый/лишний STREAMINFO и edit list.
- Те же 71 проверки прошли под ASan/UBSan полного production
  Fmp4V2Bridge/ResourceSource, -O0/-g0, detect_leaks и halt_on_error, exit 0.
- SDK: по 45 v2 unit tests на ОС; FLAC требует собственного точного профиля
  и допустимого audio kind/container. Прежние AAC-LC/Opus/video не заменены.
- Linux Qt и Windows Qt: четыре прежних standalone AAC/H.264 и два Opus/fMP4;
  native Opus conformance — по 27 проверок. Classic дополнительно прошёл
  AAC mono, Opus mono, selected optional Opus/fMP4 и v1 sessionDirectory.
- Classic: ContextMenuPrompted=1 и ContextMenuEnabled=0 заданы до первого
  запуска частных копий; HKCU commands/icons сверены до/после каждого процесса.
  Все три одноразовые tasks/stages удалены: первый прерванный прогон,
  одиночный probe и итоговый полный повтор. SHA испытанных EXE совпадает
  со сборкой. Установленные Windows EXE/INI сохранены.
- Linux UI/audio — частные X11/D-Bus/PulseAudio через run_tests.sh. Проверенный
  EXE заменён атомарно с backup и сверкой текущего INI/остальных файлов.
  Пользовательский сеанс и микшер не менялись, просмотр не прерывался.

## Ранние результаты

Первый fixture-run получил exit 1: ffprobe не выдавал duration каждого FLAC
packet. Независимый duration oracle теперь использует decoded frame nb_samples.
Следующий native-run, exit 1, показал штатный отказ FragmentIndex на fLaC;
добавлен отдельный разрешающий флаг после валидированной FLAC-конфигурации во
всех адаптерах. Ранний Windows Qt прогон до этого допуска — exit 8.
Итоговые native/Qt группы прошли на пересобранных бинарниках.

В первом Classic прогоне четыре 44.1-kHz профиля прошли; запуск mono 16-bit
48-kHz завершился session_closed («Playback was stopped or replaced»), exit 1.
Причина остановки не подтверждена; её нельзя приписывать действиям пользователя
или дефекту кодека без дополнительных данных. Отдельный повтор на побайтно
том же файле прошёл, exit 0. Затем полный повтор, начиная с 48 kHz, прошёл все
восемь профилей и проверки совместимости, exit 0. Этот ранний результат
сохранён отдельно от итоговых успешных checks.

Ограничения формата сверены с [Xiph FLAC-in-ISOBMFF](https://raw.githubusercontent.com/xiph/flac/master/doc/isoflac.txt)
и [RFC 9639](https://www.rfc-editor.org/rfc/rfc9639.html). Это ограниченный
standalone fMP4 FLAC profile; другие контейнеры, layout/rates/depths, прочие
аудиокодеки, реальная интеграция Harvester и физическая A/V-приёмка остаются открытыми.
