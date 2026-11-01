# SDK 1.62: самостоятельное FLAC-аудио в растущем fMP4

Linux Qt, Windows Qt и Windows Classic воспроизводят один настоящий required
FLAC audio track из одного scalar fMP4 resource. Harvester не изменяется.
Поддержка отдельных файлов `.flac` этим этапом не добавляется.

Проверить `schema2Playback.standalone`: `version=1`, `mode=standalone-fmp4-v1`
и `standalone-fmp4-flac` в `profiles`. Параметры профиля находятся в `flac`:
mono/stereo, 44100/48000 Hz, 16/24 bit. Общая поддержка FLAC обычным плеером
не означает поддержку недокачанного FLAC через API.

Используется [модель standalone](ProgressiveStandalone.md): `kind=audio`,
`required=true`, `codecHint=flac`, `containerHint=fmp4`, один source.resourceId.
VOD с известной duration, timebase 1/10000000, timestampOffset=0,
непрерывный подтверждённый prefix и SHA-256. Начальная длина может быть
неизвестной. Нужен head moov перед media fragments, без external init,
segments, каталогов и live. Пример SDK: `progressive-v2-standalone-flac.json`.

Проверка инициализации следует [Xiph FLAC-in-ISOBMFF](https://raw.githubusercontent.com/xiph/flac/master/doc/isoflac.txt)
и структуре STREAMINFO из [RFC 9639](https://www.rfc-editor.org/rfc/rfc9639.html).
Этот ограниченный профиль требует fLaC sample entry и первый/единственный
конфигурационный dfLa version 0/flags 0 с одним завершающим STREAMINFO длиной
34 bytes. Дополнительные metadata blocks и edit lists не включены. Проверяются
block/frame bounds, rate/channels/depth, соответствие sample entry и media clock.
Нулевые неизвестные frame-size/total-samples/MD5 поля не переписываются.

Допуск fLaC в FragmentIndex включается только для проверенной standalone
FLAC-конфигурации. Classic использует штатный AP4_FLACSampleEntry, MP4 splitter
и FLAC decoder. Qt открывает `/track/0` без видеодекодера. Stereo и частота
берутся из настоящего декодирования; исходные compressed bytes сохраняются.
Entropy decoding и проверка FLAC frames остаются в штатных backend decoders.

Pause, подтверждённый seek, update, ожидание tail, complete, natural EOF и
release используют прежний API; readerId сохраняется. Для FLAC не добавляется
аудиозапас перед seek. Тестовый writer выбирает целые fragments по независимым
packet clocks; значения duration берутся из фактического числа decoded samples,
если ffprobe не публикует packet duration. Пороги буфера плеера не меняются.

В тестах создаётся независимый целочисленный PCM-сигнал. После чтения всего
источника через progressive bridge сжатые bytes совпадают с исходным fMP4,
а повторное декодирование этих bytes в PCM совпадает с исходным сигналом
по длине и SHA-256. Для 24-bit используется точное представление в s32le.
Это проверка сохранности медиаданных, а не аттестация физического аудиовыхода.
[Результаты каждой редакции](ProgressiveStandaloneFlacValidation-2026-11-01.md).

SDK не содержит EXE/DLL/установщиков или медиа. Нужны сборки из source commit
этого SDK. Отдельный FLAC container, MP3/WAV/ADTS/другие аудиоформаты,
многоканальный звук, остальные rates/depths и прочие пункты ТЗ остаются в чеклисте.
