# Подготовка MPEG-TS: общий транспортный парсер

Внутренний этап после SDK 1.38. `src/DSUtil/HarvesterTS.h` — переносимый C++17
разбор TS/PAT/PMT/PES для будущего segment playback трёх редакций. В приложение,
`Fmp4V2Bridge`, `FragmentIndex`, IPC или Python SDK он пока не подключён.
Публичной TS capability нет; действующий SDK остаётся 1.38.

## Что реализовано

`ts::Initialization` проверяет внешний init и возвращает `ts::Program` с
валидированными PAT/PMT, PID и stream type. `ts::Media` принимает именно результат
Initialization и завершённые bytes одного медиафайла. Результат — список PES с
сырыми PTS/DTS (90 kHz), PCR (27 MHz), транспортными границами и диапазонами
элементарного payload внутри исходного TS-файла. Заголовки TS/PES и adaptation
stuffing в elementary payload не входят. PES может пересекать много TS-пакетов,
а его заголовок — границу пакета.

Проверяются:

- пакеты ровно по 188 байт, sync, TEI, scrambling, adaptation field и stuffing;
- continuity counters, включая adaptation-only пакеты; начало счётчика каждого
  отдельного сегмента независимо, дубликаты payload-пакетов не поддерживаются;
- CRC MPEG-2 PSI, pointer field, секция и её заголовок на нескольких пакетах,
  несколько повторных секций в одном payload; незавершённая секция — ошибка;
- один PAT/PMT в init, одна программа, один elementary stream, stream type
  AVC `0x1b` или ADTS AAC `0x0f`; PCR идёт на том же elementary PID;
- повторные PAT/PMT в media совпадают с init побайтно; таблицы с другим version,
  PID или descriptors отклоняются; CA descriptor также отклоняется;
- PCR до первого media payload, marker bits PTS/DTS, обязательный PTS,
  неубывающий DTS и PCR внутри сегмента, отсутствие transport discontinuity;
- длина bounded PES, заголовок и stream ID, точные диапазоны elementary payload.

Эти структурные требования следуют ограниченному профилю на основе
[описания сегментов MPEG-TS](https://www.w3.org/TR/mse-byte-stream-format-mp2t/).
Собственные ограничения первого этапа: init до 1 MiB, media до 16 MiB,
один PES до 4 MiB, до 65536 PES, PSI до 1024 байт. PCR extension 0..299.
Null packets разрешены. Дополнительные SI PID, NIT, несколько ES в одном TS,
шифрование, rollover и discontinuity не включены. Аудио с DTS отдельно от PTS
структурно допускается; его применимость проверит будущий codec layer.

У видео допускается PES_packet_length=0: границу задаёт следующий payload start
или конец завершённого сегмента. Сам по себе транспортный разбор не доказывает,
что внутри такого PES закончился валидный видеокадр.

## Что этот этап ещё не проверяет

`stream_type` в PMT не является квалификацией кодека. SPS/PPS, AAC profile,
реальная полнота/длительность access units, keyframe, смена конфигурации ES,
временная непрерывность между сегментами и соответствие declared start/duration
пока не проверяются. Не выполняются декодирование, remux, сеть или файловый I/O:
парсер принимает уже прочитанный bounded memory buffer. SHA-256 и защита
файлового identity остаются ответственностью будущего адаптера ResourceSource.

Особенно важно различать PES и кадр: в одном аудио-PES может быть несколько
ADTS access units. В реальной bundled-фикстуре 18 PES содержат 141 AAC-кадр.
Последний frame end нельзя вывести только из PTS заголовка PES.

512 детерминированных мутаций проверяют устойчивость транспорта. 389 сохраняют
валидный transport layout, 123 отклонены. Принятие мутации payload не доказывает
валидность кодека; проверка битового потока — следующий слой.

## Проверка и продолжение

[Отчёт GCC/MinGW/MSVC и ASan/UBSan](ProgressiveTsTransportValidation-2026-10-30.md).
Стенд `tests/progressive_ts_cli.cpp` — отдельный native executable, не команда
player IPC. Он не используется в обычном запуске приложения.

Следующие необходимые шаги:

1. AVC/AAC access-unit parser, постоянство конфигурации и отдельный ограниченный
   clock/index с проверкой envelope и межсегментного перехода.
2. Чтение через существующие ResourceSource/leases/integrity, staging и виртуальный
   поток; elementary payload из нескольких ranges для Classic demux.
3. Адаптеры Classic и обеих Qt-сборок: buffer/seek/complete/release, реальные
   декодеры, проверки всех трёх редакций, затем capability и SDK.
4. Отдельно — muxed A/V, HEVC и иные профили, rollover/config changes/live,
   реальные источники и длительная A/V-приёмка.

Harvester, установленный Linux-плеер и Windows-установки на этом этапе не меняются:
новый код пока не входит в приложение. Новая версия SDK или установка EXE для
самостоятельного native-парсера не требуется.
