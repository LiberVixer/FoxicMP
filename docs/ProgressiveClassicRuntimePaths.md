# Classic: временный каталог и Unicode reader CLI

7 ноября 2026. Продолжение [Qt runtime-path проверки](ProgressiveWindowsProfilePaths.md).
Принят ограниченный срез Classic TEMP120/240. Запуск воспроизведения при
TEMP320/1100 остаётся открытым; полного long-TEMP PASS нет.
[Машинный отчёт](ProgressiveClassicRuntimePathsValidation-2026-11-07.json).

## Что изменено

Изменена тестовая инфраструктура, исполняемый код установленных плееров не менялся.
Новый [Classic driver](../tests/windows/classic_runtime_paths_test.py) явно
передаёт trackReader=true. Старый directory-contract не включал reader tracking,
поэтому ожидать после него существования reader registry было ошибкой теста.
Новый тест проверяет отдельную квитанцию живого reader, явные released/filesClosed
после cancel через IPC и независимый native CLI, а затем forget. Пустая папка
и отсутствие записи не принимаются за доказательство освобождения файлов.

В [native reader CLI](../tests/progressive_reader_cli.cpp) Windows narrow argv
заменён wmain с явным UTF-8 преобразованием. Иначе filesystem::u8path получал
байты активной кодовой страницы и отвергал кириллицу с Illegal byte sequence.
MinGW target получает -municode; Windows hold использует существующий native
extended-path helper. Linux использует прежний main. Новый --temp-info напрямую
измеряет GetTempPathW/GetTempPath2W; измеритель не запускает Python с длинным TEMP.
[Общий conformance](../tests/progressive_reader_conformance.py) получил отдельные
Unicode/long-root варианты; CTest progressive-readers-unicode проверяет 320.

## Принятые проверки

| Проверка | Результат |
|---|---|
| Classic, TEMP120 / TEMP240, indexed и fragmented MP4 | 4 сценария PASS, Python native exit 0; два процесса плеера завершились native 0 |
| Reader CLI Windows: обычный / Unicode320 / Unicode1100 | По 13 проверок, каждый native exit 0 |
| Reader CLI Linux: обычный / Unicode320 / Unicode1100 | По 14 проверок, каждый native exit 0; Linux-only ACL/symlink проверка объясняет отличие числа строк |
| Финальная сборка reader CLI | Windows и Linux native exit 0 |

TEMP120/240 реально использованы: native probe сообщает 121/241 символ с конечным
разделителем, registry — 172/292 символа. Квитанции действительно читались из
этой папки; наличие prefix в запросе само по себе не считается подтверждением.
Оба контейнера начинали воспроизводить неполные файлы: 16 fragmented mdat
или 80% indexed fixture,
status clock превысил 0.2 секунды до complete. Затем опубликован остаток, принят
complete, выполнен cancel, получены положительные квитанции закрытия файлов
при живом плеере. Исходные байты сверены, удалены только тестовые файлы после
filesClosed/forget, процессы закрыты обычным WM_CLOSE. Физический A/V, Pause,
натуральный EOF и новый 2-секундный release budget этим срезом не измерялись.

Native conformance проверяет независимые readers, закрытие файлов при живом
процессе, выход владельца, indeterminate unlocked receipt, corrupt record,
изоляцию, pagination, forget и сохранность media. Эти native проверки не
подменяют запуск Classic или Qt.

## Отказы сохранены

* Первая новая попытка остановилась на native CLI Illegal byte sequence при
  TEMP120; это тестовая утилита, не доказанный отказ Classic playback.
* Следующая попытка прошла четыре playback случая, но Python --probe-temp
  с child TEMP320 не завершился за 10 секунд. Это отдельный measurement/launcher
  отказ до запуска третьего Classic, не product PASS/FAIL третьего процесса.
* После замены измерителя нативным TEMP320 дошёл до Classic capabilities,
  но start не ответил за стандартные 5 секунд. Повтор отдельного TEMP1100
  также дошёл до start, завершившегося WinError 233. Native probe TEMP1100
  измерил системный fallback C:\Users\User; requested root не использован.
  Код завершения плеера 1 записан после failure cleanup и не объявляется
  доказательством самостоятельного crash. Причина start отказов не локализована.
* Предыдущий waiting-downloader отказ из Qt/runtime отчёта не объяснён новым
  PASS: старый сценарий начинался с меньшего буфера, и это иной эксперимент.
* Промежуточная сборка Windows helper дала native exit 1 из-за неоднозначного
  DWORD overload RapidJSON; явный uint32_t исправил компиляцию. Финальная сборка 0.

По умолчанию новый driver выполняет только объявленный TEMP120/240 срез.
Открытая граница воспроизводится через --temp-lengths 320 или 1100;
это диагностический запуск, его FAIL не скрывается изменением ожиданий.
Далее нужно снять состояние потоков/подготовки Classic во время start timeout
с измеренным TEMP и отделить проблемы API/loader/source preparation.

## Сохранность

Отдельный INI с ContextMenuPrompted=1, ContextMenuEnabled=0 записывается до
первого запуска каждой копии, включая discovery. Звук тестовых копий идёт
в Null Audio Renderer; микшер/рабочий сеанс не изменяются. Все 583 protected SHA
и 158 HKCU entries совпали до/после. Четыре одноразовых task удалены,
соответствующих тестовых процессов не осталось. Runtime registry каждой
принятой копии удалён после положительных receipts/forget. После отказов
завершение owned процесса подтверждалось ожиданием ОС. Reader-status cleanup
первой попытки не принят: старая CLI не прочитала Unicode-путь; её частный
TEMP root удалён TemporaryDirectory после завершения процесса.

Новые версии приложений не создавались: Windows Classic/Qt и Linux main/INI
сохранили прежние bytes. Переустановка Linux для изменения test-only helper
не нужна. Harvester, опубликованные assets и настройки Windows не менялись.
