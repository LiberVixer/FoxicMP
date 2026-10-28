# Проверка VP8/Vorbis, 28 октября 2026

SDK 1.21, три отдельные редакции. Harvester и установленные Windows EXE/профили
не изменялись. [Поддержанные профили](ProgressiveVp8Vorbis.md).
[Полные результаты и хеши](ProgressiveVp8VorbisValidation-2026-10-28.json).

| Стенд | Результат |
|---|---|
| Linux Qt | 8 коротких CTest групп; API aggregate 122 PASS, legacy/source 19 PASS; exit 0 |
| Windows Qt | 2 CTest группы; API 113 PASS; exit 0 |
| Windows Classic | 8 SDK контрактов, 50 медиавариантов, отдельный VP8/Vorbis и legacy provider; exit 0 |
| Native media | 437 PASS обычным runner и ASan/UBSan с leak detection; exit 0 |
| WebMIndex ↔ FFprobe | 6 тестов, 9 потоков VP8/VP9/AV1/Opus/Vorbis; exit 0 |
| WebMIndex/Vorbis ASan/UBSan | 6 тестов, 8000 header mutations и пакетные проверки; exit 0 |
| Python SDK/family | 29 PASS; exit 0 |

Qt totals включают init/cleanup каждой группы, пропусков нет. Linux API разбит
на функциональную группу и 5 групп по 10 медиавариантов; параллелизм CTest — 3.
Первый длинный Linux-прогон прерван SIGTERM (143) после 32 PASS; все сценарии
затем повторены и завершены в коротких группах, итоговый exit 0. Все три редакции отдельно
проверили 50 медиавариантов: 34 прежних и 16 новых. Новые варианты:
VP8 + Opus, VP8 + Vorbis mono/stereo, VP9/AV1 8/10-bit + Vorbis stereo,
fMP4 H.264/HEVC/AV1/VP9 + WebM Vorbis, WebM VP8 + fMP4 AAC-LC/Opus.
Native-корпус отдельно проверяет Vorbis mono/stereo для всех объявленных сочетаний.

## Сквозной сценарий

Probe/start с двумя физически неполными файлами, возобновление после release,
публикация подтверждённых диапазонов, реальное воспроизведение не менее 10 с до
complete, наблюдаемая перемотка назад каждого нового варианта. Затем
переименование закреплённых ресурсов, дописывание, complete, естественный
finished, filesClosed и удаление квитанций. В Qt проверены меняющиеся кадры,
кодек/глубина декодера и декодирование Opus/Vorbis/AAC 48 kHz.
Classic проверен отдельно через DirectShow. Это не измерение физической A/V.

Индекс сравнивается с FFprobe по числу/размерам пакетов, payload SHA-256,
PTS, sync и duration с учётом отдельного Opus discard_padding. Допуск PTS
0.5001 мс, duration 1.1 мс из-за Matroska timestamp scale. Для Vorbis конец
потока может включать overlap/padding encoder и не равен физическому времени
последнего слышимого сэмпла. Полная integrity-проверка payload не заявляется.

Генератор мутаций проверяет ограниченность/безопасность parser, а не считает
любой принятый изменённый заголовок корректным декодируемым аудио. 8000 мутаций
выполнены отдельно для mono/stereo с детерминированным seed; ASan/UBSan/leaks
не обнаружили ошибок. Первые неудачные сборки/сравнения и их исправления
сохранены в JSON, итоговые результаты относятся к финальным EXE.

Classic запущен только в отдельной копии с ContextMenuPrompted=1 и
ContextMenuEnabled=0; HKCU команды/иконки установки до/после совпадают.
Тестовая копия совпала со сборкой по SHA-256. Одноразовая задача удалена,
LastTaskResult=0. Linux тестировался с частными X11/D-Bus/PulseAudio.

## Установка Linux

Атомарно заменён EXE, прежний сохранён:
`_bin/FoxicMPQt-linux-x64/foxicmp-qt.before-sdk-1.21-20261028T133015Z`.
Профиль и 93 существующих файлов проверены и сохранены.
Версия приложения остаётся 0.2.0; поддержку SDK определять по capabilities.

| Файл | SHA-256 |
|---|---|
| Linux Qt | `976d04567800c703b7794a19b01f2dbad770f1d196465ec27ac96b7186b319c3` |
| Windows Qt | `01b080de74073630e6f89cf77977e9a4758816594548f40a73fb5d1e7e382b75` |
| Windows Classic | `cc4b4f58eba4e1c772b4d57ae56849cc3fa9497eae116a7bd17a80c6916ad4d0` |
| Linux INI до/после | `ee2a51243880e578a7b1f38a0278ed178d1e2e379ba0f4f756b5f77427ac24b6` |

Другие Vorbis rates/floors, Ogg, произвольный MKV, muxed/remux, дополнительные
дорожки/metadata, live, реальные источники Harvester, физическая синхронизация
и длительный дрейф остаются отдельными этапами. Оборудование и Wayland исключены.
SDK ZIP не обновляет установщики 0.2.0.
