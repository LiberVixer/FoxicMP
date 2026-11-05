# Continuous SDK 1.82: авария настоящего FFmpeg внутри AVIO

Проверены **по восемь сценариев Linux Qt, Windows Qt и Windows Classic,
всего 24**, через обычный публичный SDK и настоящие decoder/GUI adapters.
[Результаты, исходные отказы стенда, SHA и exit codes](ProgressiveContinuousAvioFaultsValidation-2026-11-05.json).

## Где выполняется ввод-вывод

FFmpeg-worker не открывает исходный файл или output cache сам: он использует
custom AVIO и закрытый pipe-протокол. Подтверждённые source bytes читает
ResourceSource родительского процесса; готовые bytes пишет его StreamOutputStore.
Поэтому «смерть FFmpeg посреди файлового I/O» уточняется как остановка внутри
настоящего AVIO read/write callback, с проверкой последующего закрытия файлов
и очистки кэша родителем. Ошибка физического диска или остановка родителя внутри
конкретного системного pread/WriteFile этим тестом не инжектируются.

## Контрольные точки

`tests/progressive_ffmpeg_fault_worker.cpp` собирает настоящий remux source с
FFmpeg runtime, добавляя две compile-time контрольные точки. В production
translation unit macro `FOXICMP_STREAM_WORKER_TEST_CHECKPOINT` не определён:
его вызовы исключаются препроцессором. Отдельный test worker не устанавливается
и не входит в SDK/portable/.deb; CLI и capabilities production worker не меняются.

- **Read:** FFmpeg вошёл в AVIO read callback, родитель получил настоящий
  подтверждённый source block и передал его worker. Hex уже проверен и распакован,
  но callback ещё не вернул bytes в FFmpeg. Маркер содержит offset/length/SHA;
  тест независимо сверяет его с исходным файлом и опубликованным диапазоном.
- **Write:** настоящий muxer вызвал AVIO write callback. Test worker отправляет
  только префикс реального `io:write` сообщения с частью hex, без закрывающей
  кавычки/newline, и останавливается. Родитель не может принять эту незавершённую
  строку за новый cache append. Публичный writtenBytes совпадает с начальным
  offset callback; при удерживаемом кэше после смерти SHA/size прежнего cache
  prefix остаются неизменными.

Тест атомарно публикует control file с точным worker PID и типом callback только
после настоящего начала playback и ожидания хвоста. Writer физически дописывает
тот же partial с 12 до 20 s, публикует новые ranges/SHA и тем самым приводит
**того же worker** в нужную точку. Маркер публикуется только внутри callback.
После проверки parent PID, executable и удерживаемой OS identity тест завершает
только этого child: Linux pidfd/SIGKILL, Windows held handle/TerminateProcess(197).
Файлы управления находятся исключительно в отдельных test-player copies.

## Матрица и гарантии

2 callbacks × playing/manual pause × без/с внешним reader = 8 сценариев редакции.
Для Windows Qt test worker собран MinGW, для Classic — MSVC, для Linux — GCC;
production main EXE используются неизменёнными, скопированными во временные папки.

Сначала выполняются только status-запросы, без Stop/Cancel. Требуются error,
sourceFilesClosed, закрытие настоящего decoder и ноль его readers. Возможны
`backend-worker-exited` и `backend-pipe-error`: Windows может обнаружить разрыв
pipe раньше публикации OS exit. Playback обязан сообщить ту же причину; успешный
EOF/complete запрещён. Все собственные receipts подтверждают filesClosed.

В 12 сценариях второй процесс открывает настоящий output.fmp4 и регистрирует
reader с другим sessionId, связанным через physical file identity. Ошибка
производителя не должна удалить его кэш и не должна давать ложный released.
После close внешнего file handle проверяются его filesClosed receipt, настоящий
OS exit helper и автоматический release/удаление кэша status-only, без Stop.
Время до release после последнего close ограничено 2 s; в сценариях без внешнего
reader отсчёт начинается до kill и включает decoder/source/cache retirement.

Второй независимый player продолжает увеличивать позицию с прежними IDs. Поздние
heartbeat/update погибшей попытки отклоняются. SHA partial не меняется плеером;
после release writer дописывает исходный файл до конца, новая явная preparation
и Play создают новые IDs в том же живом player. Старый просмотр автоматически
не возобновляется. Close/forget и удаление временных копий проверяются.

## Проверки и поставка

Новый CTest — `progressive-continuous-sdk-avio-crash`, timeout 180 s. На Windows
для него задаётся `FOXICMP_AVIO_TEST_RUNTIME`, указывающий на отдельный portable
runtime с DLL/plugins; original main EXE и test worker передаются явно.
Linux использует `qt/tests/run_tests.sh` с частными X11/D-Bus/PulseAudio.
Classic copies до первого запуска получают ContextMenuPrompted=1 и
ContextMenuEnabled=0, страница «Форматы» не открывается. Одноразовая задача
удаляется; 158 HKCU shell entries сверяются до/после.

Инструментированный worker дополнительно прошёл девять unarmed engine сценариев
под ASan/UBSan: у двух положительных fixtures сохранены 600/720 encoded packets,
clocks и decoded frame hashes, проверены прежние cancel/error guards. Этот
sanitizer-прогон не выдаётся за проверку GUI/DirectShow под санитайзером.
Неинструментированный Linux production worker пересобран: SHA совпал побайтно
с прежним EXE. Установленные приложения/профили и публичные SDK ZIP сохранены.
Новой версии приложения и переустановки для test-only checkpoints не требуется.

Первый Linux прогон остановился из-за неверного разбора reader_status helper
самим тестом; исходник и exit 8 CTest сохранены. Первые Windows тесты также
остановились из-за слишком узкого ожидания только backend-worker-exited;
фактический backend-pipe-error после подтверждённого kill не является EOF.
Сохранены оба exit 1 и точные traces. Финальные три прогона выполнены с одинаковым
уточнённым тестом; budget/filesClosed/cache/SHA требования не ослаблялись.

Остаются системные parent file-I/O faults, более широкая race/stress matrix,
другие continuous codec/container profiles, полная приёмка ТЗ и исходный
единичный Linux heartbeat release >2 s. Этот этап не объясняет прежний outlier.
