# Непрерывный remux: точное PCM-окно AAC-LC

6 ноября 2026. Продолжение [внутреннего AAC aligned worker](ProgressiveContinuousAacAligned.md).
[Результаты, SHA, исходные отказы и границы](ProgressiveContinuousAacWindowValidation-2026-11-06.json).
Публичный SDK остаётся 1.83; новый audio mode ещё не подключён к его API.

## Что добавлено

Частный worker mode `continuous-h264-aac-window-v1` принимает H.264 и AAC-LC
mono/stereo 44,1/48 kHz с невыравненным окончанием. AAC остаётся пакетами по
1024 decoded samples; полезная длительность берётся из исходной дорожки и
фиксируется до обработки пакетов. Прежние ограничения I/O/размера и video-only
режимы сохраняются. Начальный skip ограничен 1024, duration — одним часом;
live, сложные discontinuities/смена конфигурации и другие AAC-профили не добавлены.

Общий `HarvesterAacWindow.h` задаёт две разные системы отсчёта:

| Потребитель | Начало | Конец |
|---|---:|---:|
| Сырой декодер без автоматического skip | initial skip + target | initial skip + полезные samples |
| Декодер с presentation timestamps и уже применённым skip | target | полезные samples |

Это предотвращает двойное удаление AAC priming. Расчёты целочисленные; raw
encoded end округляется вверх до 1024, разница с полезным концом — end padding.
Проверяются последовательные PTS/DTS и число пакетов. Последний пакет может
объявлять короткую длительность, полный coded frame или согласованный discard;
противоречивые metadata и преждевременный EOF не считаются успехом.

До первого media write worker передаёт `io=audio-window`: version, trackId,
rate/channels, обе системы отсчёта, encoded end и end padding. Родитель обязан
ответить `audio-window-accepted` с SHA-256 этого описания. Неверный hash или
отказ дают `audio-window-not-accepted` **без медиабайтов**. В результате остаются
`playbackSupported=false` и явный `pcmWindowRequired=true`: одних сырых fMP4 bytes
для точного AAC end trim недостаточно.

## Применение обрезки

Qt использует существующий реальный `Player::loadPcmWindowSource`: sample
rate/timebase и `atrim` по presentation sample positions, до пользовательских
фильтров и изменения output format. Семантика абсолютных pts описана в
[официальной документации FFmpeg atrim](https://ffmpeg.org/ffmpeg-filters.html#atrim).
Сам worker ничего не перекодирует и не фильтрует.

Для Classic добавлен отдельный внутренний
`CFFAudioDecoder::SetAacPcmWindow`. Он применяет raw-границы к настоящим decoded
PCM frames до преобразования формата; неверная rate/channel geometry отклоняется.
Прежний `SetPcmWindow` остаётся MP3-only. Flush/replacement очищают окно,
недостаток decoded samples при завершении — ошибка. COM/DirectShow-управление
AAC и streaming controller пока не подключены: проверялся сам decoder, не граф.

## Исправленная задержка на коротком аудио

Контрольный файл с одним полезным AAC sample и видео около 4 s сохранял видео
побайтно, но все media writes появлялись только после source complete.
Причина — interleave queue при короткой/редкой аудиодорожке. В новом windowed
mode `max_interleave_delta` установлен в 250000 µs. Это ограничение разницы
временных меток в очереди, а не обещание wall-clock latency 250 ms;
[описание параметра FFmpeg](https://ffmpeg.org/ffmpeg-formats.html#Format-Options).
После изменения законченные фрагменты доступны до complete и для этого файла.
Параметры прежних режимов не менялись. Сохранены исходный binary/source и отказ.

## Проверенная матрица

Во всех четырёх native сборках: GCC, MinGW, MSVC и ASan/UBSan. В каждом наборе
16 новых worker cases, 17 aligned AAC, 11 reordered и 9 legacy cases — по 53.
Общая геометрия: 40 комбинаций и 9830 assertions на каждую сборку.

Шесть положительных fixtures длительностью около 4,137 s:

| Профиль | Полезных PCM frames | Концевой padding |
|---|---:|---:|
| 48 kHz mono | 192137 | 375 |
| 48 kHz stereo, 30000/1001 fps | 192137 | 375 |
| 44,1 kHz mono | 176537 | 615 |
| 44,1 kHz stereo, запрошено `-bf 16`, 60000/1001 fps | 176537 | 615 |
| Выровненный контроль, H.264 без B-frames | 191488 | 0 |
| AAC первой дорожкой, один полезный sample | 1 | 1023 |

Сверены video payload/PTS/DTS/duration и decoded frames, audio payload/PTS/DTS,
геометрия окон, streaming output до complete, cancel/error и обязательный ACK.
Последняя coded audio duration сохраняется в отчёте отдельно от audible end;
она не заменяет PCM window. Перемотка streaming AV этим набором не заявляется.

Реальные Qt Player PCM adapters Windows/Linux дали по 16 точных captures:
начало, сэмпл 1 и последний полезный сэмпл, в том числе случай длиной один sample.
Выход сравнивается побайтно и имеет ровно нужную длину; после capture он не режется.
Linux FFmpeg 5.x добавляет tail даже исходнику: **только исходный oracle**
ограничивается явно заданным при генерации числом samples. Windows FFmpeg
обязан выдать это число сам. Это отличие и raw counts проверяются явно.

Настоящий Classic AAC decoder: 16 окон, шесть raw captures, 12 flush/replacement
проверок и шесть отказов truncated input. Trim bit-exact относительно raw PCM
этого декодера; отличие от независимого source decoder во всех fixtures — 0.
Допуск независимого float-сравнения — 1e-5. Прежние MP3 decoder/window/lifecycle
сценарии с новым приватным объектом декодера: 40 checks, PASS.

С новыми private workers повторены 33 SDK video-only playback cases трёх
редакций. Это совместимость прежнего API, не новый audio streaming playback.

Первый Windows fault test посчитал startup CPU как idle wait: 0,125 s при
пороге 0,1 s. Для cancel-write применяется тот же предварительный quiescence
warmup, что для положительных случаев; steady CPU budget не увеличен.
Первичные ошибки сборки private probes и все отказы сохранены в отчёте.

## Поставка и оставшаяся работа

Менялись общий trim-план, частный worker, внутренний decoder entry и тесты.
Установленные main EXE/worker/INI, исходная MpaDecFilter.lib и SDK ZIP сохранены.
Linux UI/audio проверялся только с частными X11/D-Bus/PulseAudio, захват — в файл.
Classic тестовые INI защищены от shell registration; HKCU до/после совпал.
Одноразовая задача SDK-проверки удалена. Harvester не менялся.

Управляемый parent/cache/index завершён отдельным [native этапом](ProgressiveManagedAac.md).
Следующее подключение — доставка PCM-плана в настоящие continuous Qt/Classic
entry points и DirectShow handshake, growing playback,
seek/pause/EOF/reader isolation и затем публичная capability/SDK/пакеты.
Физическая A/V синхронизация, оборудование/Wayland, длинный stress и прежние
необъяснённые latency/resource outliers этим этапом не закрываются.
