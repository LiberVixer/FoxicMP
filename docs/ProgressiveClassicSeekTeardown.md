# Classic: Stop, замена и закрытие во время seek

11 ноября 2026. Продолжение [main-window reentry](ProgressiveClassicMainReentry.md).
Проверки используют уже собранные private EXE предыдущего этапа. Production
source, normal EXE, SDK 1.87 и defaults не менялись; Harvester не затрагивался.
[Результаты, traces, native exits и сохранность](ProgressiveClassicSeekTeardownValidation-2026-11-11.json).

## Проверенные границы

Настоящий CMainFrame начинает queued seek на 21 секунду в неполном H.264 BF0
источнике. Публикация оставшихся диапазонов позволяет перейти к handoff.
Private compile-only barrier останавливает owner в одной из четырёх точек:
frame-before, graph-before, graph-after или frame-after. Как и раньше,
вложенная обработка IPC задаётся тестовой message pump, не shipping loop.

На каждой точке выполняется одно из трёх действий:

- **Stop**: штатная continuousPlaybackControl(stop) через named-pipe API.
- **Замена**: публичный start(replace=True) с отдельным complete standalone
  H.264 fMP4 resource и SHA-256. Новый source проходит штатную asynchronous
  подготовку, второй dispatch подготовленного запроса и OpenMedia, пока
  прежний seek ещё в стеке. Это другой sessionId и pinned resource/reader,
  не повторное использование old remux job.
- **Закрытие окна**: sent WM_CLOSE точному HWND, чей PID совпадает с owned
  private process. Реальные OnClose/CloseMedia и последующий main loop
  завершают процесс; принудительное убийство успешным исходом не считается.

Всего 12 основных сценариев. Подготовительный graph-before Stop smoke —
отдельный случай. Основной набор выполнен на двух private EXE, собранных
предыдущим этапом двумя способами; финальный driver дополнительно проверяет
reader receipts и отсутствие оставшегося remux cache после выхода.

## Результаты

В заданных границах новых production ошибок не обнаружено:

- После Stop старая сессия остаётся closed, pending goal снят, `applied`
  не появляется после возврата старого seek.
- Новый файл получает свой sessionId/readerId, реально начинает играть и
  ставится на паузу в собственной начальной позиции (<3 секунд). Возврат
  прежнего seek на 21 секунду не переводит новый graph туда. Старая continuous
  сессия остаётся closed; её закрытие/forget не закрывает новый source.
- WM_CLOSE завершается нормальным native exit 0 после снятия barrier.
  Driver ждёт выхода внешнего FrameScope, а не только возврата SendMessage.
- Во всех traces caller равен HWND owner, CoGetApartmentType — MAINSTA,
  frame/graph depth не превышает 1, barrier-timeout отсутствует.

Для Stop и замены SDK close подтверждает released/sourceFilesClosed старого
job, forget подтверждает освобождение job. При замене отдельно отменяется
новая regular session, перечисляются её реальные receipts: все должны иметь
released/filesClosed; они забываются через public readerForget.

У закрывающегося приложения API уже может быть недоступен, поэтому после
native exit 0 driver читает receipts отдельным native reader runner до cleanup.
Сохраняются точные reason и filesClosed. Process-exited proof, если он встретится,
нельзя считать доказательством application-level filesClosed до выхода;
это отдельное ограничение oracle. В финальном прогоне все восемь receipts
четырёх закрытий имели именно `files-closed`; recovery по process-exited
вместо application acknowledgement не использовался. Peer cleanup забывает
только закрытые leases.
Финальный прогон требует отсутствия `remux-*` в private continuous-cache root.

| Прогон | Cases | Driver exit | Main EXE exits |
| --- | ---: | ---: | --- |
| Предварительный graph-before Stop | 1 | 0 | 0 |
| Основной на reproducible private EXE | 12 | 0 | все 0 |
| Финальный с усиленным cleanup oracle на второй private EXE | 12 | 0 | все 0 |

Это 24 основных случая и один smoke. Ни kill, ни barrier timeout не засчитаны
PASS. Заданные deadlines служат защите стенда от зависания, а не полной
эксплуатационной latency/resource приёмке.

## Первичная ошибка подготовки стенда

Первый полный driver попытался подготовить второй ContinuousRemux job в том
же endpoint и получил штатный отказ `Forget the previous job first`.
Controller намеренно допускает только один job; это не дефект плеера и лимит
не ослаблялся. Прогон завершился driver exit 1 до injection замены, после одного
успешного Stop. Исходник этого driver, log и partial artifacts зафиксированы
отдельно; это не успешный полный runtime прогон.

Для проверки именно замены файла выбран поддержанный public start другого
prepared resource. Код Harvester, controller, UI, graph и message pump при
этом не изменялись. Окончательные отчёты создаются в новых каталогах; ранние
не переименовывались и не перезаписывались. Исходные video files совпали до/после.

## Сохранность и сборки

Используются private instrumented main-reentry EXE; их SHA сохранены в отчёте.
Новые application objects не компилировались. Compile-only hooks уже проверены
предыдущим MSVC normal token proof и в shipping projects не включены.
Этот этап добавляет только отдельный Python driver и документацию.

Private INI до первого запуска содержит ContextMenuPrompted=1 и
ContextMenuEnabled=0; null audio/Null Video Renderer записываются только в
копии временного профиля. Форматы/Apply не открывались. Состояние 587 защищённых
файлов и 158 HKCU shell entries совпало до/после. Установленные EXE/INI,
микшер/дисплеи/сеанс, Harvester и scheduler tasks не менялись. Нового Linux
binary нет, переустановка не требуется.

## Воспроизведение

Сначала собрать private main-reentry EXE по
[инструкции предыдущего этапа](ProgressiveClassicMainReentry.md). Затем Windows
Python, FFmpeg/FFprobe, SDK/modules текущего checkout и native reader runner:

```bat
python -B tests\windows\classic_seek_teardown_test.py --player PRIVATE_STAGE\FoxicMP64.exe --reader-runner READER_TEST_EXE --report-directory NEW_REPORT
```

`--phase` и `--mode` позволяют повторить одну пару; `--smoke` проверяет только
graph-before Stop. Driver создаёт private copies и привязывает HWND/IPC к PID.
Обычный EXE без private hooks для этого barrier-теста не подходит; его
невозможность создать ready marker не является отказом плеера.

## Оставшиеся границы

Тест не доказывает существование такой nested message pump в обычном renderer
или COM. Не проверены все промежуточные COM/filter callbacks, одновременная
физическая I/O-задержка, hardware renderers, AAC/other codec teardown,
WM_QUIT внутри barrier и полная многократная replacement/crash/race/stress
матрица. Close проверяет выход процесса, не восстановление намеренно убитого
процесса. Физическая A/V sync и старые latency/resource outliers остаются
отдельными пунктами полного ТЗ. Квота seek/receipt retention не расширялись.
