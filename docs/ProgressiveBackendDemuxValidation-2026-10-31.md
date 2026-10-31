# Проверка backend demux диагностики — 31 октября 2026

SDK 1.51, приложение 0.2.0. [Контракт](ProgressiveBackendDemux.md),
[машинный отчёт](ProgressiveBackendDemuxValidation-2026-10-31.json).
Начат пункт 2 [чеклиста](ProgressiveCompletionChecklist.md).
**Новый playback/remux не реализован и не объявлен готовым.**

## Результаты

| Проверка | Linux Qt / GCC | Windows Qt / MinGW | Windows Classic / MSVC |
|---|---|---|---|
| API реального плеера: 24 сценария | PASS | PASS | PASS |
| Native common adapter: 24 сценария | PASS | PASS | PASS |
| Worker: 5 сценариев с нормальным выходом | PASS | PASS | PASS |
| Transport: 8 проверок, включая 20 успешных и 20 неуспешных запусков | PASS | PASS | PASS |
| Resource/segment API regressions | PASS | PASS | Прежние playback/v1 ниже |
| Сохранение активной сессии и отзывчивости Qt UI при probe | PASS | PASS | Не измерялось этим Qt-тестом |

В обеих Qt-редакциях итоговый CTest-прогон: **6/6, exit 0**. Каждый API-тест
Qt: 3 passed, 0 failed. Classic проверен в отдельной интерактивной копии с
context-menu prompt/registration выключенными до первого запуска; helper — MSVC.
Его API-набор завершился с exit 0. Результат Linux не подменяет Windows.

Реальные входы: H.264/AAC MP4, MOV, Matroska и TS; VP9/Opus WebM; audio-only
M4A/AAC, ADTS/AAC, MP3, Ogg/Vorbis, FLAC, WAV/PCM. На каждом входе первые 12
пакетов сравниваются с `ffprobe`: SHA-256, bytes, stream index, PTS, DTS и duration
в исходных ticks. Это 11 проверенных входов, а не обещание любого codec/container.

Дополнительно проверены: недоступный header, fast-start до complete, moov в
неподтверждённом хвосте, publication/retry, cancel/release, запрет свободных
http/hls/concat/lavfi hints, повреждённый файл, полный физический файл без
публикации хвоста, sparse hole и SHA-256 mismatch. Readers после forget удаляются.

Фактические FFmpeg versions:

| Runtime | avformat | avcodec | avutil |
|---|---|---|---|
| Linux | 60.16.100 | 60.31.102 | 58.29.100 |
| Windows | 63.1.102 | 63.1.102 | 61.1.102 |

В JSON сохранены также SHA-256 build configuration, packet reports и хеши
исходников, основных EXE и helper каждого варианта сборки.

## Остановка и память

Проверены зависший worker, заблокированная запись, cancel, deadline, преждевременный
выход, отсутствующий EXE и слишком длинный frame. Cancel — менее 2 секунд
(в этих прогонах около 0,1 секунды), deadline — около 4 секунд. Handles/fds после
серии запусков не растут; Windows CRT/AppCompat предварительно прогреваются как
для успешного, так и для отсутствующего EXE.

ASan+UBSan common-adapter: **24 PASS, exit 0**. Отдельный worker-тест с обычным
завершением, чтением stderr и leak detection: **5 PASS, exit 0**. Отдельный
transport-тест под sanitizers: **8 PASS, exit 0**. Системные FFmpeg DLL/so сами
не пересобирались с sanitizers; это не fuzzing или аудит всего FFmpeg.

В промежуточных прогонах найдены и устранены Windows pipe deadlock, неверное
подключение MinGW CRT headers при MSVC-сборке и проблема времени жизни dependency
registries при явной выгрузке FFmpeg DSO. Теперь библиотеки живут до завершения
одноразового worker. Первые неуспешные exit codes и причины перечислены в JSON;
отдельно отмечены исправления test harness, включая PATH частного Xvfb.

## Прежнее воспроизведение и установка

- Linux Qt: все 8 shared fMP4 Opus/video профилей, 10 QtTest passed, exit 0.
- Windows Qt: H.264/Opus stereo и VP9 10-bit/Opus mono fractional,
  4 QtTest passed, exit 0.
- Classic: shared fMP4 AVC/HEVC Main10/AV1 10-bit/VP9 10-bit + AAC,
  а также v1 fragmented/indexed sessionDirectory, exit 0.
- Python ResourceProbe SDK: 16 unit tests на Linux и 16 на Windows, exit 0.

Это regressions существующих playback-профилей, не воспроизведение новых
обычных MP4/аудиоформатов через diagnostic adapter.

Установленный Linux EXE обновлён атомарно, рядом добавлен worker. Прежний EXE
оставлен для отката; INI SHA-256 до/после совпал, 209 прочих файлов сохранены.
Ни один просмотр не останавливался. Во время замены активных экземпляров этого
установленного EXE не было. Сохранённые файлы `integration/` автоматически не
заменялись; новый Python SDK распространяется отдельным ZIP. Отдельная чистая
CMake-установка также проверена: worker рядом с EXE, актуальный Python helper и
новые документы в integration, runtime-info/capability согласованы, exit 0.

Установленные Windows EXE/профили не обновлялись. В тестах Classic проверялось
равенство команд/иконок HKCU до/после; обе одноразовые задачи и временные копии
удалены. Настройки MPMY/MPC/MPC-BE, рабочий стол и системный микшер не менялись.
Harvester не менялся. Wayland, физические устройства и реальная длительная
A/V-приёмка в этот этап не входят.

## Что остаётся

Подключение backend-пакетов к воспроизведению/локальному remux, точная
классификация исхода, обычный MP4 seek, расширенная codec matrix и оставшиеся
пункты чеклиста. SDK 1.51 ZIP содержит клиент и документацию, без новых
установщиков/EXE/DLL. Установщики 0.2.0 не включают этот этап.
