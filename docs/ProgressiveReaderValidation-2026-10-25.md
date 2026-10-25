# Runtime reader leases: результаты проверки

25 октября 2026. [Точные результаты, exit codes и SHA-256](ProgressiveReaderValidation-2026-10-25.json).
[Контракт SDK 1.4](ProgressiveReaderLeases.md), [план интеграции](ProgressiveIntegrationPlan.md).
Область — opt-in player-owned v1; воспроизведение schema 2 не объявляется готовым.

## Проверки

| Проверка | Exit code | Результат |
|---|---|---|
| Linux Qt / Windows Qt / Classic | 0 каждый | Три сборки приложения; Classic с зависимыми DSUtil/BaseSplitter/MP4Splitter |
| Linux Qt, private X11/D-Bus/PulseAudio | 0 | 18 API tests passed, 0 failed/skipped; плюс model и readers |
| Windows Qt, native/offscreen, отдельные профили | 0 | 19 API tests passed, 0 failed/skipped; плюс model и readers |
| Native reader corpus, Linux GCC | 0 | 14 проверок реальных процессов/файлов |
| Native reader corpus, Windows MinGW и MSVC | 0 каждый | По 13 проверок; POSIX permissions/symlink case только на Linux |
| Native reader corpus, ASan/UBSan | 0 | 14 проверок, detect_leaks=1 |
| Общая модель v2 | 0 | Прежние 71 вектор и raw duplicate-key |
| Classic, отдельная копия и одноразовая задача | 0 | Новый reader contract, 62 validate, 100 RPC, реальный SDK/writer до complete |
| Python SDK | 0 | 12 unit tests |
| Семейный контракт | 0 | 2 unit tests |

Native corpus создаёт два процесса с открытыми файлами и независимыми leases.
Снятая блокировка при живом процессе и без filesClosed остаётся indeterminate.
После принудительного завершения ОС подтверждает смерть одного читателя, второй
сохраняет удержание. Нормальное закрытие подтверждается ещё при живом процессе.
Отдельные процессы запросов читают сохранённые квитанции; проверены повреждённый
JSON, обход пути, постраничная выдача 19 записей, изоляция sessionId, запрет forget
живого читателя и unknown_reader после подтверждённого forget. Media не меняются.

Qt проверяет реальные источники двух окон/профилей и удерживает дополнительный
shared_ptr после Stop: reader не освобождается до закрытия последних media-handles.
После освобождения одного второй остаётся занят. Новый объект API читает ту же
квитанцию; файлы для этого повторно не открываются.

Один Python reader-contract выполняется через публичный клиент во всех трёх
редакциях. Повтор start сохраняет readerId; новое поколение того же sessionId
без replace отклоняется, с replace создаёт новый ID. Release старого reader не
закрывает замену. Запоздавшие update/heartbeat/status/error/cancel/release с прежним
readerId отклоняются; новое видео остаётся активным. Source registry содержит
реальные video/audio file ID, а не переданные клиентом имена или PID.

Полные Qt API прогоны сохраняют прежние indexed/fMP4 directory-сценарии, legacy/provider,
настоящий playing до complete, SDK writer и release acknowledgement. SDK writer
теперь использует tracked start и подтверждает files-closed перед forget.
Classic writer прошёл >=10 секунд позиции до complete, затем finished и released;
точные показания и resource IDs сохранены в JSON. Это не измерение физического A/V.

Отдельно SDK проверяет потерю ответа start: status/close не выдают released=true.
Close обращается по readerId, кэширует подтверждение и забывает только освобождённую
квитанцию. Writer events и updates передают readerId. Старые capabilities сохраняют
прежний маршрут без добавления полей, которых старый плеер не понимает.

Первый native test завершился exit 1 из-за ошибки стенда: он ожидал JSON-ответ от
команды quit, завершающей процесс без ответа. Ожидание исправлено; последующие
GCC/MinGW/MSVC и sanitizer прогоны прошли. При итоговой проверке расширена защита
старого reader с release на остальные writer-команды; после неё выполнены новые
сборки и финальные API/Classic прогоны, а не засчитан прежний результат.

## Установка и оставшиеся границы

Linux EXE заменён атомарно с резервной копией, INI совпал до/после по SHA-256 из JSON.
Просмотр не останавливался, desktop mixer/сеанс не менялись. SDK/docs обновлены
отдельно. Приложение остаётся 0.2.0; новые сборки различаются SHA-256 и capabilities.
Рабочие Windows EXE/INI не заменялись. Classic тестировался с ContextMenuPrompted=1,
ContextMenuEnabled=0, регистрация установленного приложения в HKCU до/после совпала.
Одноразовая задача удалена; установщики 0.2.0 не перепаковывались.

Реальные файлы/registry сейчас связаны с player-owned v1. Подключение динамических
ресурсов v2, безопасное удаление сегментов live-окна, управляемое I/O, demux/remux
и расширенные кодеки остаются следующими этапами. Legacy-reader не становится
tracked автоматически. Потерянная/забытая квитанция не является доказательством
освобождения, один профиль не доказывает отсутствие других читателей. Принудительная
перезагрузка ОС/очистка runtime-каталога не тестировались; отсутствие записей после
них трактуется консервативно. Реальный Harvester не изменялся и не запускался.
