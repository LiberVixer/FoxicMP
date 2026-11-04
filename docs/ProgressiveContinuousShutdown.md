# Continuous SDK 1.82: закрытие player, crash и повторный запуск

Проверен ограниченный локальный MP4/H.264 video-only continuous-remux:
**восемь сценариев отдельно в Linux Qt, Windows Qt и Classic**, exit 0 каждого
прогона. [Измерения, SHA, исходные отказы стенда и exit codes](ProgressiveContinuousShutdownValidation-2026-11-04.json).
Production code, EXE и установленные профили на этом этапе не менялись.

## Сценарии и критерии

`tests/progressive_continuous_shutdown_conformance.py` проверяет обычное закрытие
окна и принудительное завершение принадлежащего тесту процесса в четырёх состояниях:

- подготовка с подтверждёнными четырьмя байтами и живым worker;
- настоящий просмотр с декодированными кадрами до writer complete;
- ручная пауза настоящего просмотра;
- фактическое `buffering` у неопубликованного хвоста после просмотра кадров.

Обычное закрытие — WM_DELETE_WINDOW на приватном X11-сервере Linux либо WM_CLOSE
окну с точным PID тестового Windows-процесса. Нормальный exit code player — 0.
Принудительный exit подтверждён через Popen.wait: Linux -9, Windows 1. Выход worker
подтверждается удерживаемым pidfd/process handle, а не поиском PID. Windows worker
при управляемом прекращении может иметь exit 1: он отдельно записан и не считается
успешным EOF медиа.

На каждом сценарии проверяются:

1. Завершение player за <2 s и наблюдение выхода worker за отдельные <2 s после
   завершения player. Это раздельные бюджеты, не общая приёмка teardown ≤2 s.
2. Независимый второй процесс продолжает реальное воспроизведение с теми же
   playbackId/readerIds и увеличивает позицию. Между сценариями его ставят на паузу.
3. SHA недокачанного оригинала не меняется при закрытии/падении player. Тестовый
   writer затем физически дописывает тот же файл, проверяет bytes и публикует
   новые SHA/ranges. Writer identity/generation сохраняются: writer не падал.
4. После запуска **того же приватного профиля** новая подготовка удаляет прежний
   journaled cache до явного опроса/подтверждения тестом старых reader receipts.
   Старые job heartbeat/update отклоняются; прежний playback не восстанавливается.
5. При нормальном выходе старые receipts имеют положительные `filesClosed` и
   `files-closed`. После crash освобождение доказывается ОС (`process-exited`),
   без выдачи его за выполненный callback нормального закрытия.
6. Prepare не запускает decoder. Только следующий явный Play создаёт новые
   playbackId/readerIds; оригинал остаётся неизменным, новый close/forget завершён.

Автоматическое восстановление прежней позиции/паузы этим результатом не заявлено.

## Найденная граница очистки

После части **нормальных** выходов cache directory с журналом остаётся на диске:
Linux Qt — preparation/buffering; Windows Qt — preparation/playing/paused;
Classic — preparation. Все медиа-handles этих нормальных выходов закрыты,
а следующий запуск того же профиля безопасно удаляет прежний кэш при подготовке.
После crash старый кэш также удаляется при проверенном повторном запуске.

Это подтверждает ограниченное восстановление, но **не полную очистку кэша до
нормального выхода**. Этот пункт остаётся открытым наряду с прежним единичным
Linux heartbeat release >2 s. Следующие шаги: завершение очистки при обычном
выходе с сохранением отзывчивости, worker crash/mid-I/O/races, внешние удерживаемые
readers и длительная stress matrix. Полное progressive-ТЗ не закрыто.

## Воспроизведение и изоляция

В Linux тест запускают через отдельный CTest driver и `qt/tests/run_tests.sh`,
с приватными X11/D-Bus/PulseAudio. Обычный рабочий стол/микшер не используются.

```text
python3 tests/progressive_continuous_shutdown_conformance.py --edition qt \
  --player /absolute/build/foxicmp-qt \
  --reader-runner /absolute/build/foxicmp-progressive-reader-tests \
  --report-directory /absolute/new-report-directory
```

Windows требует доступного интерактивного тестового сеанса. Qt использует одну
проверяемую EXE с двумя приватными профилями. Classic использует два отдельных
каталога/EXE/INI: `--edition classic --player COPY1 --player COPY2`; перед первым
запуском обязательны `ContextMenuPrompted=1`, `ContextMenuEnabled=0`. Форматы не
открываются. Снимки 158 HKCU shell entries до/после совпали. Одноразовая задача и
её stage удалены; периодических задач нет. Установленные Windows EXE/INI, Linux
EXE/INI, SDK ZIP и Harvester сохранены.

Два первоначальных Linux отказа были ошибками выбора окна стендом: libmpv создаёт
второе X11-окно с тем же PID и именем `foxicmp-qt`. Теперь выбирается окно с
брендированным заголовком приватного чистого профиля. Windows уже прошёл восемь
сценариев до этой Linux-only поправки. Отчёт сохраняет точные SHA обоих реально
исполненных вариантов; Windows-ветка и общие assertions идентичны текущим.
Финальная правка комментария не меняет Python AST проверенного Linux-варианта.
