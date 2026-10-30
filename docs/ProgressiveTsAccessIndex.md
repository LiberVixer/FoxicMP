# Подготовка MPEG-TS: AVC/AAC access units и clock/index

Внутренний этап после [транспортного парсера](ProgressiveTsTransport.md), без
подключения к плеерам или IPC. SDK остаётся 1.38; TS playback capability не
объявлена. Общий C++17 `src/DSUtil/HarvesterTSCodec.h` использует проверенные
PAT/PMT/PES и диапазоны payload из `HarvesterTS.h`.

## Ограниченный профиль

AVC: Baseline/Main/High, 8-bit 4:2:0, progressive, POC type 0 или 2. SPS содержит
VUI timing и `fixed_frame_rate_flag=1`, частота 1..240 fps. Номинальная длительность
кадра вычисляется рационально из `2 * num_units_in_tick / time_scale`.
Обычный поток с flag=0 этот профиль отклоняет, даже если наблюдаемые DTS похожи
на CFR: последняя длительность не угадывается по соседнему кадру.

Каждый video PES содержит ровно одну access unit с AUD в начале. Первый кадр
сегмента — IDR, перед ним есть SPS/PPS. Проверяются размеры/кроп, поля SPS/PPS,
RBSP/emulation prevention, ссылки PPS/SPS, AUD, первые поля slice header,
согласованность нескольких slices одного picture. Повтор конфигурации должен
совпадать побайтно; смена параметров требует другого профиля. SEI и filler имеют
проверку framing, но их полный семантический разбор не выполняется.

AAC: MPEG-4 AAC-LC по ADTS header, 32/44.1/48 kHz, mono/stereo, без ADTS CRC,
один raw_data_block в ADTS frame. В одном PES разрешены несколько целых кадров.
Кадр не пересекает PES; отдельный audio DTS, MPEG-2 ADTS, PCE channel configuration
и другие режимы на этом этапе отклоняются. Для каждого кадра сохраняются как
полные ADTS ranges, так и ranges payload без семибайтного заголовка. Длительность
— 1024 samples; время внутри PES вычисляется по суммарному числу samples, без
накопления округления от каждого кадра.

Структуру полей можно сверить с [ADTS parser FFmpeg](https://raw.githubusercontent.com/FFmpeg/FFmpeg/master/libavcodec/adts_header.c),
[AVC parameter sets FFmpeg](https://raw.githubusercontent.com/FFmpeg/FFmpeg/master/libavcodec/h264_ps.c)
и существующим `src/DSUtil/VideoParser.cpp`. Новый компонент не зависит от этих
платформенных реализаций или библиотек FFmpeg во время исполнения.

## Время, индекс и публикация

`AccessUnits` получает validated Program, полностью прочитанный TS media buffer,
фиксированный origin в 90 kHz и необязательную прежнюю конфигурацию. Возвращает
конфигурацию, access units, диапазоны исходных bytes, presentation/decode envelope
и Sample в 100 ns. B-кадры индексируются в decode order; presentation cadence
проверяется после сортировки по PTS. Для квантования TS используется допуск
112 единиц по 100 ns — округлённый вверх один tick 90 kHz. Допуск проверяется
также относительно единого начала ряда и общего числа кадров: ошибка
округления не может накапливаться от кадра к кадру или от сегмента к сегменту.
Для больших VUI числителя/знаменателя рациональная длительность вычисляется
без умножения, способного переполнить 64-bit промежуточный результат.

`AccessIndex::append` сначала полностью проверяет кандидата: codec/config,
межсегментный presentation/decode переход и declared start/duration. После этого
публикует единицы, byte offsets, конфигурацию и готовую временную границу. Ошибка
кандидата сохраняет прежние данные. Декларированные временные границы и origin
не могут подменять фактический PES clock. Перебазирование, rollover и live не
включены.

Индекс задаёт offset относительно виртуального `init + media1 + media2 + ...`.
`availableEnd(prefix)` консервативно ограничивает readiness подтверждёнными
транспортными диапазонами и PTS/DTS следующей единицы. AAC внутри одного PES
становится доступным только вместе с полным PES. `seekSample` выбирает IDR перед
video target; для audio — первый кадр, чей end больше target. После последнего
аудиокадра возвращается конец индекса, а не самостоятельный сигнал file EOF.

Ограничения: до 65536 access units на сегмент, до 2 млн единиц и 2 млн сохранённых
payload ranges в индексе; сохраняются прежние TS limits 1 MiB init, 16 MiB media,
4 MiB PES. Индекс не содержит блокировок, I/O или отмены: владелец должен
сериализовать доступ и выполнять разбор на worker, прежде чем подключать его
к UI/Source. Теоретический глобальный предел не выдаётся за многочасовой тест.

## Проверка и дальнейшая работа

[Отчёт](ProgressiveTsAccessIndexValidation-2026-10-30.md): GCC, MinGW, MSVC,
ASan/UBSan, 74 новых сценария, 512 мутаций заголовков и семь реальных
FFmpeg/ffprobe-фикстур. Проверены Baseline/Main/High, B-кадры, 29.97 fps,
AAC mono/stereo и 32/44.1/48 kHz, а также несколько последовательных сегментов.

Это framing и разбор заголовков, а не entropy decoding. Валидность всех
macroblocks/reference lists, CABAC/CAVLC и AAC spectral data/implicit SBR ещё
не доказывается. Принятые мутации slice payload не объявляются пригодными для
декодера. Физическая A/V sync, реальные сетевые источники и долгое воспроизведение
не проверялись.

Далее — ResourceSource/leases/integrity, staging и виртуальное чтение, затем
Classic и обе Qt-сборки: реальные декодеры, buffer/seek/complete/release и только
после этого capability/SDK. Muxed TS, другие кодеки, VFR/POC1/interlaced,
смена конфигурации и live остаются отдельными расширениями. Harvester, профили
и установленные приложения этот внутренний этап не меняет.
