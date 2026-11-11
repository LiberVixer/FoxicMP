# Авария worker внутри Classic seek и завершение общей транзакции

11 ноября 2026. Продолжение [AAC teardown](ProgressiveClassicAacTeardown.md).
Изменён общий `ContinuousPlayback::applyQueuedSeek`, используемый Classic,
Windows Qt и Linux Qt. Публичные API/SDK 1.87 и формат профиля сохранены.
Harvester не затрагивался.
[Результаты, исходные отказы, SHA и native exits](ProgressiveSeekWorkerFailureValidation-2026-11-11.json).

## Ошибка и исправление

Новый private Classic сценарий завершает только свой remux worker, пока
настоящий CMainFrame/DirectShow seek удерживается на проверочной границе.
После обнаружения `backend-worker-exited` воспроизведение правильно получало
ошибку, однако возвращавшийся accepted seek ещё мог записать `seekState=applied`.
Повторная проверка перед вызовом графа не закрывала изменение producer state
внутри самого вызова. Отказ сохранён на прежнем EXE, без ослабления oracle.

Теперь после accepted handoff общая транзакция повторно читает состояние
output, producer error и собственную ошибку. Ошибка сохраняется как seek error;
retiring/removed/stopped output отменяет ожидавшую цель. Перед записью снова
проверяется актуальность intent и owner: reentrant observer не может стереть
новую цель или revision. Принятый графом growing seek по-прежнему расходует
ровно один слот из прежних 16, даже если источник успел отказать. Физически
принятая графом операция не объявляется отозванной задним числом.

Это общая поправка завершения транзакции, а не изменение декодера, меню,
оформления, пользовательских defaults или протокола Harvester.

## Проверка внутри настоящего Classic

`tests/windows/classic_seek_teardown_test.py --mode worker-crash` добавляет
opt-in сценарии; прежний набор Stop/replace/close по умолчанию сохранён.
Используются прежние compile-only frame-before, graph-before, graph-after и
frame-after barriers. Никакой nested message loop в обычный EXE не добавлен.
Это принудительный IPC reentry на MAINSTA, а не полный набор естественных
COM/renderer interleavings или физический disk-I/O fault.

Два AAC-LC профиля прежнего этапа: 48000 Hz mono/BF2/25 fps и 44100 Hz
stereo/BF3/30000/1001 fps, video 160×90, 24.137 s, audible samples=rate×24+137.
До инъекции подтверждаются playing, pause и `pcmWindowApplied=true`.
Video-only профиль служит отдельной регрессией.

OS process handle проверяет executable и parent PID worker перед
TerminateProcess с намеренным кодом 197. Проверки не завершают чужие workers
или установленный плеер. Error обнаруживается публичным status без Stop;
источник закрывается, успешный EOF запрещён. После снятия barrier pending seek
не остаётся applied, decoder readers закрыты, source/cache receipts получают
files-closed и кэш освобождается до явного SDK close/forget.

Предел для обнаружения/закрытия источника — 2 s с учётом TerminateProcess;
для освобождения после возобновления стека — ещё 2 s. Время искусственного
удержания stack frame не выдаётся за естественную latency приложения.
Внутри barrier живой decoder lease может законно удерживать кэш. Если status
возвращает cache-in-use, тест требует действительно незакрытый reader и
не считает его filesClosed. После выхода из seek этот hold должен исчезнуть.

Первый smoke ошибочно требовал released для кэша до снятия собственного
barrier: исходный файл был закрыт, но cache-in-use оставался обоснованным.
Эта ошибка стенда хранится отдельно от следующего, реального отказа applied.
Ни один из этих запусков не засчитан успешным.

После автоматического release тест явно закрывает/забывает старый job и
готовит новый из тех же подтверждённых bytes. Подготовка сама не возобновляет
playback. Только явный play запускает новую сессию с новыми job/playback/reader
IDs и собственной начальной позицией <3 s; старый target=21 s не наследуется.
Повторный worker тоже закрывается штатно; exact exit сохраняется отдельно
от намеренного worker exit 197 и нормального main exit 0.

## Воспроизведение

Нужен private EXE по [инструкции MAINSTA](ProgressiveClassicMainReentry.md),
пересобранный с текущим shared header, Windows Python, FFmpeg/FFprobe и native
reader runner. Использовать новые private report directories:

```bat
python -B tests\windows\classic_seek_teardown_test.py --player PRIVATE_STAGE\FoxicMP64.exe --reader-runner READER_TEST_EXE --audio-profile 48000-mono --mode worker-crash --report-directory NEW_REPORT_48
python -B tests\windows\classic_seek_teardown_test.py --player PRIVATE_STAGE\FoxicMP64.exe --reader-runner READER_TEST_EXE --audio-profile 44100-stereo --mode worker-crash --report-directory NEW_REPORT_44
python -B tests\windows\classic_seek_teardown_test.py --player PRIVATE_STAGE\FoxicMP64.exe --reader-runner READER_TEST_EXE --mode worker-crash --report-directory NEW_REPORT_VIDEO
```

Общий native тест `tests/progressive_queued_commit_test.cpp` проверяет новые
post-handoff failures/retirement и замену intent внутри final output/producer
observer. Native return Applied означает принятие операции графом; успешное
завершение пользовательской цели определяется отдельно по seekState/seekError.
Qt проверяется своим общим native кодом и обычными UI/SDK regressions;
Classic barrier injection не выдаётся за реальный Qt callback.

## Результаты

- Восемь AAC и четыре video-only worker-death сценария, ещё один AAC smoke:
  PASS. Все 13 намеренных worker exits — 197, main exits — 0. Worker явного
  retry завершается с 1 при отмене неполного источника; это не успешный EOF.
- Повторены 36 прежних Stop/replace/WM_CLOSE сценариев: PASS, main exits 0.
- По 32 SDK сценария обычных Classic/Windows Qt/Linux Qt (queued seek,
  video-only, B-frames и AAC), всего 96: PASS. Ещё два raw-file Classic API
  случая прошли с normal main exit 0. Отдельные idle SDK peers штатно убираются
  самим harness через kill: Windows exit 1 / Linux -9, это записано отдельно.
- GCC, MSVC и MinGW: по 137 shared native checks, exit 0. Тот же новый тест
  с прежним shared header отказал на проверке 118, exit 1.
- Qt UI: по 28 passed, 0 failed, 0 skipped на Windows/Linux. Linux CTest
  включает шесть выбранных tests, exit 0; используются private X11/D-Bus/Pulse.

В 13 успешных инъекциях max source-close с учётом kill — 94 ms; max release
после снятия barrier — 94 ms. Это максимум данного ограниченного прогона,
не статистический p95 и не гарантия для всех источников/устройств.

В первой полной серии исправленного EXE обрыв pipe первым дал
`backend-pipe-error`; слишком узкий oracle ожидал только worker-exited.
Оба кода уже предусмотрены существующими AVIO crash tests. Итоговый driver
допускает только эту пару, требует того же кода в producer и playback, OS exit
197 и прежние release/seek-state ограничения. Частичная серия не засчитана
полной; все 12 случаев повторены в новых каталогах. В итоговой серии был
получен backend-worker-exited во всех случаях.

## Границы и установки

Точный состав прогонов и измерения приведены в JSON. Это ограниченный
worker-death/seek срез, не полная AAC/crash/race/stress приёмка. Проверки
не измеряют PCM sample values, физическую A/V синхронизацию, аппаратный вывод,
неподтверждённый CancelIoEx или отказ внутри каждой дисковой инструкции.

Для Classic используются только private INI с ContextMenuPrompted=1 и
ContextMenuEnabled=0; Форматы/Apply не открываются. Windows installations
не заменяются. После успешных Linux проверок меняется только установленный
Linux EXE, атомарно и с проверенной резервной копией; INI и остальные личные
файлы сохраняются. Qt/Classic theme, микшер, дисплеи, desktop session и
Harvester не меняются. Scheduler tasks не создаются.

После тестов совпали все 587 защищённых файлов и 158 HKCU shell entries.
После установки отличается только разрешённый Linux EXE: остальные 586 файлов
сохранили SHA. Все прочие файлы установленного Linux каталога также сверены
отдельно; версия/CLI capabilities прежние, просмотр не останавливался.
