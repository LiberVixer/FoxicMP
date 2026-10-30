# Проверка MPEG-TS playback — 30 октября 2026

SDK 1.39 включает [ограниченный сегментный AVC/AAC-профиль](ProgressiveTsPlayback.md)
во всех трёх редакциях. Номер приложения остаётся 0.2.0. Harvester не изменён.

| Среда | Проверка | Exit code |
|---|---|---|
| Linux Qt, GCC, частные X11/D-Bus/PulseAudio | 4 playback-сценария, меняющиеся декодированные кадры, AAC stereo 48/44.1 kHz | 0 |
| Windows Qt, MinGW, offscreen | Те же 4 сценария, decoded video/audio format и меняющиеся кадры | 0 |
| Windows Classic, MSVC, отдельная безопасная копия | Те же 4 сценария, работа графа, продвижение позиции и decoder I/O обеих дорожек | 0 |
| Linux GCC native | 8 bridge + 26 ResourceSource + 74 codec/index + 65 transport | 0 |
| Windows MinGW native | Те же 8 + 26 + 74 + 65 | 0 |
| Windows MSVC native | 8 bridge-сценариев | 0 |
| Linux ASan/UBSan, leak detection | 8 bridge-сценариев, без ошибок sanitizers | 0 |
| Python SDK v1/v2 + family contract | 15 + 25 + 2 теста | 0 |

Четыре playback-сценария в каждой редакции: Main 25 fps с B-кадрами и AAC 48 kHz;
High 30000/1001 fps с AAC 44.1 kHz; Main с отрицательным DTS/аудиопрероллом 80 ms;
40-секундный Main-каталог из 82 ресурсов. Всего 12 сценариев плееров. Во всех
проверены игра до complete, pause/update, backward seek в паузе, resume,
ожидание недокачанного хвоста, forward seek после публикации и natural EOF
с подтверждённым release. Inline-сценарий также проверяет дыру в префиксе.

Qt дополнительно проверяет H.264/yuv420p, AAC stereo с нужной sampling rate и
изменение hashes framebuffer. Classic проверяет настоящий граф и I/O через
обе дорожки; отдельный capture его pixels/физического аудиовыхода не выполнялся.
Это не измерение физической A/V sync.

## Нативные байты, clocks и ошибки

В каждом из четырёх compiler/sanitizer прогонов сверены 3531 access units:
75+142, 90+131, 75+142 и 1000+1876. Нативный стенд проверяет prefix immutability,
прежний FragmentIndex, append/complete/EOF, случайные backward byte reads и
last-owner leases. Виртуальные MP4 декодируются FFmpeg; video и PCM hashes
совпадают с исходными TS. Для preroll reference PCM обрезается на 3840 samples.
Максимальная фактическая разница PTS/DTS с ffprobe в этих фикстурах — 132 ×
100 ns (13,2 µs) на AAC 44.1 kHz; в preroll-профиле — 1 × 100 ns.

26 ResourceSource-сценариев отдельно проверяют SHA, замену/усечение/перезапись
вытесненного ресурса, caller buffer при ошибке, отмену, ошибку конструктора и
постраничный retry. Уровень ресурсов проверен на 242 ресурсах/семи страницах;
engine playback каталога проверен на 82 ресурсах, не на предельных 8192.
Одновременно остаётся не больше восьми media/init resource leases.

Проверка отрицательного начала обнаружила реальную ошибку первого адаптера:
аудиошкала 10 MHz приводила к `skip_samples=800000` вместо 3840 при 80 ms/48 kHz.
Исправление использует AAC sampling rate, точные 1024 samples на AU и одно
округление origin. Тест теперь явно проверяет `skip_samples`, PCM hash после
обрезки и воспроизведение во всех трёх редакциях. Заявленный допуск к TS index
учитывает TS tick и округление audio origin до половины сэмпла.

Ранние ошибки Python-стенда (standalone import path) и определения каталога
исправлены до окончательных прогонов. Для Linux использованы уже существующие
частные Xvfb tools; desktop/микшер не перенастраивались. Итоговые коды всех
сборок, прогонов, установки и очистки равны нулю.

## Совместимость, установка и границы

На обеих ОС прошли девять прежних native suites: segmented fMP4, catalog,
segment staging, scalar media, WebM segments и TS layers. Отдельные playback
регрессии: Linux v1 SDK writer + fMP4 H.264/AAC + WebM VP9/Opus; Windows Qt
v1 writer + scalar VP9/Opus + segmented H.264/AAC и VP9/Opus; Classic v1
directory/API сценарий. Совместимые CLI/API endpoints и пользовательские
настройки не заменялись.

Linux EXE обновлён атомарно до проверенной сборки. Предыдущий EXE сохранён как
`foxicmp-qt.before-sdk-1.39-20261030T190853Z`. INI до/после совпадает по SHA-256,
197 сопутствующих файлов сохранены; просмотр не прерывался. Номер EXE — 0.2.0,
capabilities установленной копии совпадают со сборкой SDK 1.39.

Windows-установки не заменялись. Classic тестировался в отдельных каталогах
с `ContextMenuPrompted=1`/`ContextMenuEnabled=0`; HKCU shell registration
проверена до и после. Одноразовые задачи и staging-копии удалены; SHA-256
проверенной Classic-копии совпадает с итоговым build EXE.

[Полные результаты, exit codes и SHA-256](ProgressiveTsPlaybackValidation-2026-10-30.json).
Не проверены реальные сайты, многочасовые записи, физическая A/V sync,
предельная ёмкость индекса, hardware и Wayland. Muxed A/V, live, смена codec/init,
VFR/interlace/POC1 и clock rollover не входят в этот профиль. Полностью готовый
очень большой каталог и частые большие updates ещё требуют оценки стоимости
worker-подготовки и копирования metadata/index.
