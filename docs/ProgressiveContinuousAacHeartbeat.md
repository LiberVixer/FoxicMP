# Continuous AAC 1.84: heartbeat, результаты Qt и открытый gate Classic

**Общая приёмка остаётся открытой.** Linux Qt и Windows Qt прошли по 17
AAC-сценариев с точным exit 0. У Classic пять функциональных сценариев прошли,
после четырёх успешных повторных paused expiry пятый превысил ресурсный лимит:
**10 threads у второго preparing endpoint при baseline 7 и допуске +2**.
Факт, baseline, неуспешный замер и последующее наблюдение сохранены;
порог не увеличивался, повтор не заменяет исходный FAIL.
[Полные результаты, исходные ошибки, SHA и exit codes](ProgressiveContinuousAacHeartbeatValidation-2026-11-06.json).

Профиль один: H.264 B3 / 30000/1001, AAC-LC stereo 48 kHz, неполное последнее
PCM-окно. Обычные production EXE и десять bundled SDK modules 1.84;
приложение 0.2.0. Код приложений и опубликованные пакеты не менялись.

## Проверенные сценарии

Пять функциональных случаев: истечение heartbeat при preparation / playing /
manual pause; продление явным heartbeat около 20 s и публикацией около 40 s;
completed paused playback без heartbeat более 32 s, затем seek/Play/EOF.
Чужой generation не продлевает срок, status-запросы не изображают writer,
после expiry нужны новые явные playback/readers. Deadline — настоящий 30000 ms,
часы не сокращались и не подменялись.

Серия рассчитана на 12 paused expiry на тех же двух PID с baseline после
функционального прогрева; полностью её прошли обе Qt-редакции. Это отдельный прогон, не baseline после 100 циклов.
Требуются automatic release без Stop/Cancel до подтверждения, filesClosed
receipts, exit worker, decoder close, удаление кэша, forget, SHA оригинала и
отказ поздним writer events. Второй endpoint продолжает подготовку неполного
source с heartbeat; он не играет видео в данном стенде.

Прежние бюджеты: handles/fds +8, threads +2, RSS/private +256 MiB от baseline,
средний CPU parent ≤1 ядра, каждый записанный RPC и release <2 s. Метрики
записываются до assert. После отказа выполняются только пассивные дополнительные
замеры; последующая очистка не превращает FAIL в PASS. Linux private metric —
RssAnon; Windows — PrivateUsage. Эти пороги не доказывают отсутствие всех малых
утечек и не ограничивают peak CPU/RAM всей ОС.

## Исправление гонки наблюдений в стенде

В отдельной диагностике Classic обнаружен ложный отказ `state == paused`:
producer-status успел вернуть healthy до deadline, а следующий playback-status
уже увидел `writer-heartbeat-timeout`. Это два последовательных наблюдения.
Helper теперь разрешает только этот точный error после настоящего deadline и
продолжает обычный poll producer. Ранний/иной error, отсутствие terminal producer,
нарушение deadline или filesClosed по-прежнему дают FAIL.

Детерминированный контроль удерживает **клиентский** healthy reply через границу
30 s; EXE/worker и часы не меняются. Старый helper получил exit 1 с той же
ошибкой, исправленный — exit 0. Новый публичный regression test повторён на
всех трёх редакциях, каждый exit 0. Дополнительно старый video-only default Linux
проверен до и после исправления без передачи media-mode, оба exit 0.
RPC trace различает source error и decoder error; release-интервалы относятся
к клиентским наблюдениям, а не к неизвестному внутреннему моменту OS close.

## Что показала диагностика потоков

Первый ресурсный FAIL — рост threads у preparing endpoint Classic. Его связь
с прежним MPCVR Event growth не установлена. В другой серии после ложного oracle FAIL число потоков
второго процесса за минуту простоя снизилось с 11 до 9; завершились потоки со
start address в ntdll/combase. Это не реконструкция состава потоков первого
отказа: его процессы к этому моменту завершились.

Дополнительный пассивный прогон с исправленным helper: один прогревочный и
шесть paused expiry, затем 60 s простоя. Сохраняются thread IDs, start modules /
offsets и метрики до/после; product hooks и suspend потоков не используются.
Его baseline отличается от первого строгого прогона. Это наблюдение, **не
повторная приёмка 12 expiry**; отсутствие нарушения относительно более высокого
baseline не объясняет исходный отказ. Связь первого 7→10 с системными потоками,
его устойчивость и критерий полного resource gate остаются открытыми.

## Исправление сохранения exit code

Первый Windows launcher использовал Start-Process / WaitForExit / Refresh и
получил null вместо ExitCode. Отдельный child с явным `exit 7` воспроизвёл null;
сам launcher завершился кодом 1. Нативные коды его Python-процессов не
восстанавливаются по JSON: в отчёте они явно unknown. Windows Qt полностью
повторён с прямым `$LASTEXITCODE`: все 17 случаев, exit 0. Диагностические и
regression Windows-прогоны также записывают точные коды напрямую.

## Повторение и сохранность установок

`tests/progressive_aac_package_test.py` принимает `--scenario aac-heartbeat`
(5 функциональных +12 paused expiry) и `--scenario aac-heartbeat-ordering`
(принудительное пересечение deadline). Нужны `--edition`, `--player`,
`--reader-runner`, `--sdk-directory`, `--report-directory`.
Старые результаты закреплены за прежними helper/driver SHA, новые — за
исправленным helper. Старый helper, частные диагностические драйверы и точная
последовательность задержки сохранены в validation JSON.

Linux проверен через `qt/tests/run_tests.sh`, частные X11/D-Bus/PulseAudio;
AAC decoder и PCM window подтверждены, `pulse` на частном sink. Windows Qt —
`wasapi` с volume 0. Classic — две отдельные копии с Null Audio Renderer,
ContextMenuPrompted=1 / ContextMenuEnabled=0 до первого запуска; Formats не
открываются. Все 583 защищённых файла, пользовательские профили и 158 HKCU
command/icon entries сохранены. Все собственные одноразовые задачи удалены.
Установленный EXE не заменялся: новой версии приложения здесь нет.
YouTubeHarvester, Windows-настройки и desktop не менялись.

Остальные rates/layouts, HD/4K и многочасовой soak, физическая A/V синхронизация,
OS I/O faults, прежние Linux release >2 s и MPCVR Event outliers остаются открытыми.
Wayland и отложенное оборудование не проверялись.

## Дополнение 7 ноября 2026

[Точный повтор с символами и проверка после простоя](ProgressiveClassicThreadStability.md)
установили природу роста 7→10: три TppWorkerThread; EXE-start count 6 неизменен.
После 123 s потоки и handles вернулись к 7/427. Старый мгновенный gate остаётся
FAIL. Отдельный Classic профиль проверил все 17 cases и threads +2 после ≥120 s
без jobs/IPC, сохранив немедленные прочие лимиты и все instant overruns; exit 0.
