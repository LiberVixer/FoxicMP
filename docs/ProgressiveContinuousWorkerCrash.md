# Continuous SDK 1.82: аварийное завершение worker

Проверены четыре сценария отдельно в Linux Qt, Windows Qt и Windows Classic:
подготовка с четырьмя опубликованными байтами, настоящий просмотр, ручная пауза и
реальное ожидание неопубликованного хвоста. Все **12 сценариев прошли**, exit code
каждого финального прогона — 0. [Измерения и SHA](ProgressiveContinuousWorkerCrashValidation-2026-11-04.json).
Исполняемые файлы и production code в этом этапе не менялись.

## Методика

`tests/progressive_continuous_worker_crash_conformance.py` использует обычные
SDK/IPC endpoints двух отдельных тестовых процессов. Завершается только worker
первого процесса. Перед этим сверяются его родительский PID и файл executable;
удерживается OS identity. Linux отправляет SIGKILL через pidfd; Windows вызывает
TerminateProcess с кодом 197 через тот же проверенный process handle. Процессы
по имени не ищутся и не завершаются. Выход worker подтверждается ОС.

Сначала выполняются только read-only status-запросы, без Stop/Cancel. В пределах
2 s ожидаются ошибка `backend-worker-exited`, положительные `released` и
`sourceFilesClosed`, ноль decoder readers и отсутствие private cache. Каждый
reader receipt должен иметь `filesClosed=true` и `reason=files-closed`.
Промежуточное `cancelling` при уже известной ошибке допустимо до завершения
асинхронной очистки; окончательное состояние producer — `error`, не успешный EOF.

В финальном прогоне сумма измеренных времени принудительного завершения worker
и последующего status-only ожидания release составила **0,047–0,297 s**. Это
измерение конкретных четырёх состояний данного профиля, не всей численной матрицы
ТЗ. Playback status отдельно подтверждает закрытие decoder и причину ошибки.

Параллельный второй процесс увеличивает позицию с прежними playbackId/readerIds.
Исходный partial сохраняет SHA; тестовый writer затем физически дописывает тот же
файл и публикует новые ranges/SHA. Поздние heartbeat/update погибшей попытки
отклоняются. Новая подготовка не возобновляет старый просмотр, а явный Play
создаёт новые job/playback/reader IDs **в том же живом процессе плеера**.
Следующие close/forget завершаются, оригинал остаётся неизменным.

## Стенд и воспроизводимость

Новый Qt CTest — `progressive-continuous-sdk-worker-crash`, timeout 120 s. Linux
выполняет его через `qt/tests/run_tests.sh` с частными X11/D-Bus/PulseAudio. На
Windows тот же Python-сценарий отдельно выполнен с Qt и защищёнными копиями
Classic. CTest также зарегистрирован в Windows-конфигурации; регистрация через
`ctest -N` не выдаётся за дополнительный runtime-прогон.

Первый тест Classic завершился exit 1 из-за слишком раннего требования
`state=error`: он увидел `cancelling` с правильным `backend-worker-exited` до
закрытия decoder. Исходный тест, traceback и release trace сохранены в отчёте.
Исправлено только ожидание перехода; предел 2 s, фактические receipts и все
проверки удаления кэша сохранены. Финальные прогоны всех трёх редакций выполнены
с одинаковым исправленным исходником теста.

Classic получает `ContextMenuPrompted=1` и `ContextMenuEnabled=0` до первого
запуска. Страница «Форматы» не открывается. До/после сверяются 158 установленных
HKCU shell entries. Временная одноразовая Windows-задача и её тестовые копии
удаляются после прогона. Установленные EXE/INI, публичные SDK ZIP и Harvester
сохранены. Установленный Linux EXE совпадает с проверенным; новой сборки и замены
EXE для этого изменения тестов не требуется.

## Оставшаяся граница

Проверен bounded local MP4/H.264 video-only continuous профиль. Принудительная
смерть worker посреди конкретного pipe/file I/O, гонки publication/Stop/replacement,
surviving external readers, длительная stress-матрица и другие codecs/containers
остаются открыты. Прежний единичный Linux heartbeat release >2 s этим прогоном
не объяснён и не закрыт. Полная приёмка ТЗ не объявляется завершённой.
