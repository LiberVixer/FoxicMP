# Закрытие и падение плеера с ожидающим или выполненным seek

11 ноября 2026. Дополнение к [continuous shutdown/recovery](ProgressiveContinuousShutdown.md)
после [post-handoff исправления](ProgressiveSeekWorkerFailure.md) и
[двух дополнительных queue stress профилей](ProgressiveQueuedProfileStress.md).
Обычные приложения 0.2.0 и SDK 1.87; production/SDK modules не меняются,
Harvester не затрагивается.
[Измерения, native exits, receipts и сохранность](ProgressiveQueuedShutdownValidation-2026-11-11.json).

## Новый срез

Тестовый launcher `tests/progressive_aac_package_test.py` получил opt-in
`--shutdown-queued`, допустимый только с `--scenario aac-shutdown`.
Без флага прежние восемь preparation/playing/paused/buffering normal/crash
сценариев сохраняются. С флагом к ним добавляются четыре случая:

- обычное закрытие с ожидающей целью 19 s;
- обычное закрытие после фактически выполненного growing seek на 19 s;
- принудительный crash с той же ожидающей целью;
- crash после выполнения growing seek.

Применяется прежний AAC-LC 48000 Hz stereo / H.264 BF3 / 30000/1001 профиль,
video 160×90 около 24 s. Это один профиль, не повторение всех AAC layouts.
В отличие от range-only queue stress, здесь writer физически дописывает
неполный файл, fsync и новые SHA/ranges публикуются через обычный SDK.

## Ожидающий и выполненный переход

Первоначально записан только prefix до video packet на 12 s. Плеер реально
играет, затем ставится на ручную паузу с применённым PCM window adapter.
Запрос на недоступные 19 s получает revision 1 / waiting-range / quota 16.
Для waiting случая исходный decoder reader/позиция сохраняются до закрытия.

Для applied случая writer дописывает prefix до packet на 20 s и публикует
revision 2. Тест дожидается настоящего paused playback на 19 s с погрешностью
<120 ms, отсутствующим pending target и quota 15. Это проверка позиции API,
не измерение sample-value PCM или физического кадра на мониторе.

Обычное закрытие — WM_DELETE_WINDOW только на private X11 либо WM_CLOSE
окну точного owned PID в Windows. Crash — Popen.kill своего тестового main
process. Main exit и наблюдение worker exit имеют отдельные бюджеты <2 s;
их сумма не выдаётся за общий teardown deadline <2 s.

После normal exit cache должен исчезнуть до нового prepare, а старые receipts
получить положительные filesClosed/files-closed. После crash состояние
process-exited не считается выполненным callback закрытия файла. Для queued
случаев проверяется объединение producer reader IDs и фактических decoder IDs:
Qt может создать новые readers при growing seek/reload, старые закрытые
квитанции тоже остаются частью доказательства.

## Writer и явный повтор

После выхода плеера writer продолжает тот же файл: waiting case — до prefix
20 s, applied case — до полного физического файла. Generation/session/writer
identity сохраняются, меняются revision/ranges; writer не падал. Физическая
полнота сама по себе не помечается writer complete.

Запускается тот же private profile и новая подготовка. Старый journaled cache
удаляется до ручной проверки/forget старых receipts. Prepare открывает только
source reader; автоматического запуска или возобновления позиции нет.
Старые job heartbeat/update и playback status отклоняются.

Только явный Play начинает новую попытку с новыми job/playback/reader IDs,
startSeconds=0, seekRevision=0, pending=null и quota 16. Позиция подтверждена
между 0.2 и 3 s, даже когда прежняя цель 19 s уже доступна после дописывания.
Затем приходят поздние Stop/cancelSeek/seek с прежним playbackId. Все три
отклоняются как unknown-continuous-playback; новая попытка с теми же readers
продолжает двигаться. Её close/forget требуют положительные release proofs.

Независимый второй endpoint действительно играет во время закрытия/падения
первого и продвигает позицию с прежними playbackId/readers. Это короткий
контроль независимости, не многочасовой параллельный просмотр.

## Повторение и изоляция

```text
python3 -B tests/progressive_aac_package_test.py --edition qt --player /PRIVATE/foxicmp-qt --reader-runner /PRIVATE/foxicmp-progressive-reader-tests --sdk-directory /PRIVATE/integration --scenario aac-shutdown --shutdown-queued --expected-audio-output pulse --report-directory /PRIVATE/NEW_REPORT
```

Linux запускается только через `qt/tests/run_tests.sh` и private CTest driver
с X11/D-Bus/PulseAudio. Windows Qt для WM_CLOSE использует native windows QPA:
offscreen QPA не предоставляет обычный HWND. Первоначальный offscreen запуск
остановился на выборе окна, exit 1; это ошибка конфигурации стенда, не PASS
и не ошибка нормального закрытия плеера. Повтор использует новый каталог.

Classic запускается отдельными copies с ContextMenuPrompted=1 и
ContextMenuEnabled=0 до первого запуска, private null audio/video. Форматы/Apply
не открываются. Windows-установки и пользовательские профили не затрагиваются.
Linux проверяет установленный EXE через новые profiles, без замены binary.
Микшер, дисплеи, desktop settings, Harvester и scheduler tasks не меняются.

## Результаты

Все три редакции прошли по 12 сценариев: четыре новых queued case и восемь
прежних shutdown cases. Всего 12 новых +24 regression. Полный набор повторён
после добавления явного ограничения новой позиции <3 s после поздних команд;
сохранены обе серии по 36 случаев и SHA реально выполненных вариантов.
Конечные drivers Windows/Linux exit 0.

В последней серии 18 normal main exits=0, 12 намеренных Windows crashes=1 и
шесть Linux crashes=−9. Max main termination=0.172 s; max worker observation
после выхода main=0.031 s, каждый проверен своим прежним лимитом 2 s.
Это максимумы ограниченного прогона, не общий p95 teardown или sum≤2 s.

Все 18 normal exits удалили свой cache до повторного запуска; все 18 crash
cases оставили journaled cache, который удалился при новом prepare до ручной
проверки квитанций. Сохранены 51 files-closed и 43 process-exited receipts.
Закрытые до crash прежние decoder receipts остаются files-closed: не все
квитанции crash case обязаны иметь одинаковый reason.

Windows worker exits: 12 с кодом 1 и 12 с кодом 0. Нулевой worker exit после
смерти parent не доказывает media EOF: sourceComplete/outputComplete в этих
случаях false. Linux pidfd подтверждает завершение, но не даёт native worker
exit code; ноль ему не приписывается. Cleanup оставшихся idle main peers
использует отдельный намеренный kill, записанный в processHistory.

Все 587 защищённых файлов и 158 HKCU shell entries совпали до/после.
Новых ошибок приложения в этом наборе не обнаружено. Установленный Linux
EXE уже актуален; новая сборка и переустановка для тестовых изменений не нужны.

## Границы

Это обычный публичный SDK/Player lifecycle, без compile-only CMainFrame hooks,
подмены часов или искусственного I/O barrier. Crash выбран после подтверждённого
waiting/applied состояния, а не в каждой внутренней дисковой/COM инструкции.
Автоматический resume прежней позиции не реализуется и не заявляется.
Power-loss/durable checkpoint, зависший disk I/O, другие AAC layouts/codecs,
физический A/V и полная crash/race/stress матрица остаются открытыми.
