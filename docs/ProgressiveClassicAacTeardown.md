# Classic: AAC при Stop, замене и закрытии внутри seek

11 ноября 2026. Расширение [video-only teardown](ProgressiveClassicSeekTeardown.md)
на настоящий H.264/AAC graph. Production source/EXE, defaults и публичный
SDK 1.87 не менялись; Harvester не затрагивался.
[Трассы, native exits, worker proofs и сохранность](ProgressiveClassicAacTeardownValidation-2026-11-11.json).

## Профили и сценарии

Используется прежний private CMainFrame/splitter barrier EXE. Driver получил
опцию `--audio-profile`; значение `video` сохраняет прежний video-only путь.

| Профиль | Video | AAC-LC | Длительность video / audible sample count |
| --- | --- | --- | --- |
| 48000-mono | H.264 BF2, 25 fps, 160×90 | 48000 Hz, mono | 24.137 s / 1152137 |
| 44100-stereo | H.264 BF3, 30000/1001 fps, 160×90 | 44100 Hz, stereo | 24.137 s / 1058537 |

Генератор создаёт неполный AAC sample endpoint намеренно: audible samples =
rate×24+137. Сначала подтверждён prefix до video packet на 12 секунде; физический
хвост уже существует, но доступен worker только после публикации ranges.
ContinuousRemux выбран в AAC window mode с required video/audio tracks.

Перед инъекцией подтверждаются audioWindowReady, точные sampleRate/channels/
presentation samples, реальное состояние playing и `pcmWindowApplied=true`.
После pause признак PCM window должен оставаться применённым. Это подтверждение
работы штатного Classic AAC window adapter, не измерение слышимого звука или
sample-value oracle. Все тестовые profiles используют null audio/Null Video
Renderer; desktop audio device и пользовательский mixer не открываются тестом.

На каждом профиле выполняются 12 сочетаний четырёх frame/graph before/after
boundaries и трёх действий: public Stop, public start(replace=True) и owned
sent WM_CLOSE. Pending seek на 21 секунду начинает исполняться после publish.
Как в предыдущем этапе, private message pump задаёт reentry внутри настоящего
SetPositions. Обычная nested message loop в shipping приложении не добавлялась.

## Замена со звуком

Новый regular resource — complete muxed fMP4 с обеими required H.264/AAC-LC
дорожками, SHA-256 и явным hint `fmp4-muxed`. Он имеет другой sessionId,
resource binding и reader, но использует те же encoded media для изоляции
проверки перехода между owners от различий содержимого.

После replacement driver требует новый sessionId/readerId, playing с позицией
0.15–3 секунды, `audioCodec=aac-lc` и положительные decoderIoBytes отдельно для
video и audio. Затем новый graph ставится на паузу, его позиция остаётся <3:
возвращающийся старый seek на 21 секунду не переходит в новый файл. Старый
continuous playback остаётся closed, без ложного applied. Отмена/forget старого
job не закрывает новый regular reader.

Public counters доказывают чтение audio payload; сами по себе они не заменяют
сравнение decoded PCM. Отдельного PCM capture и проверки float/sample values
в этом этапе нет. Частоты/channels проверяются по immutable plan старого
continuous source; для replacement проверяются его declared media codec и
фактическое чтение обеих дорожек.

## Остановка, закрытие и освобождение

После Stop сессия остаётся closed без pending target. После WM_CLOSE owned
main process должен выйти с native exit 0. Нельзя засчитать принудительный
kill, timeout barrier или cleanup как успешное выполнение сценария.

Driver теперь удерживает OS handle конкретного remux worker с момента ready.
После выхода main он требует worker termination и сохраняет точный native
workerExitCode через GetExitCodeProcess. Worker код при отмене не подменяется
кодом main или driver: каждый сохраняется отдельно. Сам факт завершения
worker не доказывает, что он успешно дочитал media до EOF — источник здесь
остаётся downloading и намеренно отменяется.

Во всех 37 успешных сценариях main завершился с кодом 0, а worker — с кодом 1
при отмене неполного источника. Эти коды worker не считаются успешным EOF.

Для живого плеера SDK close подтверждает released/sourceFilesClosed, forget —
удаление job; новая regular session отдельно освобождает свои receipts.
После выхода main native reader runner проверяет каждую оставшуюся lease до
cleanup, сохраняя reason/released/filesClosed. Требуется пустой remux cache.
Значения post-exit receipts и любые ограничения reason oracle приведены
в отчёте. Все 24 post-exit receipts полного набора получили `files-closed`;
fallback `process-exited` не использовался, оставшиеся remux caches отсутствуют.

## Проверки

| Прогон | Cases | Driver exit | Main exit |
| --- | ---: | ---: | --- |
| AAC 48000 mono graph-before Stop smoke | 1 | 0 | 0 |
| AAC 48000 mono, полный набор | 12 | 0 | все 0 |
| AAC 44100 stereo, полный набор | 12 | 0 | все 0 |
| Прежний video-only набор на обновлённом driver | 12 | 0 | все 0 |

Это 24 основных AAC case, 12 regression и один smoke. Каждый проверяет owner
PID/HWND, MAINSTA, frame/graph depth ≤1, отсутствие barrier-timeout, сохранность
исходных файлов, нормальный main exit и фактическое завершение своего worker.
Runtime cleanup и его native helpers принадлежат только этим private copies.

Первая попытка полного набора задала `fmp4` для двух дорожек одного replacement
resource. Плеер корректно вернул «Use one video and one to eight separate audio
resources». Это ошибка snapshot стенда: для совместного ресурса необходим
`fmp4-muxed`. Driver exit 1 сохранён отдельно, лимиты/плеер не ослаблялись.
Исправленный прогон использует новые каталоги; ранние log/artifacts не
перезаписываются и не считаются успешным полным тестом.

## Сохранность и повторение

Private INI до первого запуска содержит ContextMenuPrompted=1 и
ContextMenuEnabled=0. Форматы/Apply не открывались. Все 587 защищённых файлов,
включая профили, и 158 HKCU shell entries совпали до/после; результат в JSON. Установленные
Windows/Linux copies не заменялись. Qt production не менялся, нового Linux
binary нет и переустановка не требуется. Harvester, микшер/дисплеи/сеанс и
scheduler tasks не менялись; периодических/одноразовых задач не создавалось.

Использовать собранный private main-reentry EXE по
[прежней инструкции](ProgressiveClassicMainReentry.md), Windows Python,
FFmpeg/FFprobe и native reader runner:

```bat
python -B tests\windows\classic_seek_teardown_test.py --player PRIVATE_STAGE\FoxicMP64.exe --reader-runner READER_TEST_EXE --audio-profile 48000-mono --report-directory NEW_REPORT_48
python -B tests\windows\classic_seek_teardown_test.py --player PRIVATE_STAGE\FoxicMP64.exe --reader-runner READER_TEST_EXE --audio-profile 44100-stereo --report-directory NEW_REPORT_44
```

Без `--audio-profile` сохраняется video regression. `--phase`, `--mode` и
`--smoke` позволяют повторить выбранную границу. Обычный EXE без private hook
не подходит для barrier oracle; это не ошибка обычного плеера.

Полная AAC waiting/replacement/crash/race/stress matrix, sample-value PCM oracle,
HE-AAC/прочие layouts, физические I/O-барьеры внутри AAC projection, неполучивший
подтверждения CancelIoEx драйвер, произвольные COM/renderer interleavings,
WM_QUIT внутри barrier и длительные resource/latency outliers остаются открытыми.
Квота 16 и receipt retention не расширялись. Новая production ошибка в этом
ограниченном AAC teardown наборе не найдена.
