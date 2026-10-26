# Проверка реестра и cleanup — 26 октября 2026

[SDK 1.9](ProgressiveRetirement.md), базовый commit
`88eeb0a4b2cf1df271b30819714d18036e7bd225`.
[Машинный отчёт, SHA-256 и результаты](ProgressiveRetirementValidation-2026-10-26.json).
YouTubeHarvester и установленные Windows EXE/профили не менялись.

## Проверки

| Редакция / стенд | Результат | Exit |
|---|---|---:|
| Linux Qt | Полный API 51 PASS; native retirement, readers; после FIFO-уточнения — retirement, registry UI 8, реальный v2 playback 3 | 0 |
| Windows Qt | Полный API 52 PASS; native retirement, readers, registry UI; после FIFO — retirement, registry UI 8, реальный v2 playback 3 | 0 |
| Windows Classic | SDK/reader/resource/v1/v2/publication/preparation regression; финально 5 настоящих OS I/O-gates и отдельный legacy/provider playback | 0 |
| Native retirement | 6 проверок GCC / MinGW / MSVC; финальный ASan+UBSan с leak detection | 0 |
| Python SDK / семейный контракт | 35 / 2 теста | 0 |

QtTest числа включают init/cleanup. Новая registry-группа содержит шесть сценариев:
четыре reader-команды, задержанное уничтожение и исчерпание/восстановление ёмкости.
Полные Qt/Classic regression выполнены до последнего уточнения FIFO-очереди;
финальные целевые проверки повторены после него. Отдельные профили обязательны;
Linux UI/audio запускались через частные X11/D-Bus/PulseAudio.

## Что проверяют тесты

- Пока registry worker остановлен тестовым gate, UI timer/status/capabilities
  отвечают; второй registry-запрос получает reader_registry_busy.
- Живой reader не удаляется; старый release не закрывает заменивший его источник.
- Последний владелец отдаёт Source без ожидания заблокированного деструктора.
  Пока реальное закрытие задержано, released=false и Forget запрещён;
  после закрытия доступны положительный receipt и Forget.
- Все 32 active/queued/running слота учитываются; 33-й Source отклоняется до
  вызова конструктора. Одновременно уничтожаются максимум два объекта.
  После очистки повторный start работает; ошибка конструктора возвращает слот.
- V2 writer физически оставляет оба fMP4-файла недокачанными: playing >=10 секунд,
  затем complete, terminal finished и подтверждение закрытия. Revision >2^53
  и generation UINT64_MAX сохраняются; сетевых запросов нет.
- Python retry касается только четырёх reader-команд с reader_registry_busy,
  имеет общий deadline, не превращает unknown_reader в release.

## Classic: реальные операции Windows

На отдельной копии Classic использованы Windows oplocks: четыре раза native
открытие receipt задерживалось до закрытия тестового oplock-handle. Пятый случай
блокировал открытие `.guard` в фактическом уничтожении Source после команды Close.
Это не sleep перед I/O: тест проверяет OS-событие break и незавершённость операции.
Метод соответствует документации Microsoft о
[FSCTL_REQUEST_OPLOCK_LEVEL_1](https://learn.microsoft.com/windows/win32/api/winioctl/ni-winioctl-fsctl_request_oplock_level_1)
и [подтверждении oplock break закрытием handle](https://learn.microsoft.com/en-us/windows-hardware/drivers/ifs/acknowledging-oplock-breaks).

Все **15 WM_NULL** получили ответ; максимальное ожидание на стенде **7.5614 мс**.
Пока cleanup заблокирован, receipt содержит filesClosed=false; после разблокировки
readerRelease подтверждает filesClosed=true и released=true. Это результат стенда,
не универсальная временная гарантия. Все пять случаев и legacy/provider повторены
на финальном EXE; его SHA совпадает с собранным файлом.

Legacy/provider дошёл до position100ns=100212031 при readyEnd=120000000 до complete,
после complete получил readyEnd=200000000; filesClosedLeaseReleased=true.
INI тестовой копии заранее отключал контекстную регистрацию; Formats не открывалась.
HKCU команды/иконки установки совпали до/после. Одноразовая задача удалена,
LastTaskResult=0. Рабочий стол и микшер пользователя не перенастраивались.

## Промежуточные ошибки

Первый Qt build завершился с 1 из-за совпадения имени slots с макросом Qt;
имя заменено на entries, финальные сборки и тесты прошли.
Один финальный Classic harness завершился с 1: секундная выборка статуса попала
в нормальное buffering на позиции 11.0125 с и пропустила короткое playing-окно.
Условие теперь требует ранее наблюдавшегося playing и позиции >=10 с в playing
или buffering. Повтор всей финальной группы и legacy завершился с 0.

## Установка и ограничения

Linux EXE заменён атомарно, прежний сохранён для отката, просмотр не останавливался.
Версия остаётся 0.2.0, расширение определяется capabilities и SHA-256.
INI до/после: `ac9387b8d09f63317ed8ea3b681abad96e5abe8890400d4db6540017e3acde75`.

Проверки не закрывают весь F-01: legacy opening/старые блокировки, renderer/graph
shutdown, неподдержанные codec/live/remux, физическая A/V-синхронизация и реальный
Harvester остаются отдельными задачами. Windows installers 0.2.0 не обновлены этим SDK.
