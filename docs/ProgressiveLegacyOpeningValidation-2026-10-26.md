# Проверка фонового legacy opening — 26 октября 2026

[Контракт SDK 1.10](ProgressiveLegacyOpening.md). Базовый commit
`165ccb8f28c82b25c73fe1577352eadbdf0efe07`.
[Машинный отчёт и SHA-256](ProgressiveLegacyOpeningValidation-2026-10-26.json).
Работа только в FoxicMP: Harvester и установленные Windows EXE/профили не менялись.

## Результаты

| Стенд | Проверка | Итоговый exit |
|---|---|---:|
| Linux Qt | Полный API 58 PASS, legacy/source GUI regression 19; финально legacy opening 11 и реальный v2 playback 3 | 0 |
| Windows Qt | Полный API 59 PASS, v2 playback 3, legacy opening 12; финальная усиленная legacy-группа 12 | 0 |
| Windows Classic | Четыре открытия с настоящим OS oplock, отдельно provider playback до complete и полный SDK/reader/resource/v2/preparation/publication regression | 0 |
| Python SDK / family contract | 35 / 2 теста | 0 |

Числа QtTest включают init/cleanup. В legacy-группе семь управляемых сценариев
открытия и обычный provider playback: complete/failed на Linux, дополнительно
provider restart на Windows. Linux запускается через частные X11/D-Bus/PulseAudio,
Windows Qt и Classic — с отдельными профилями.

Полный Linux API и source regression предшествовали последнему добавлению
runtime `legacyPreparationPending`; после него повторены legacy opening и v2 playback.
Полный Windows API включает это поле. Затем на обеих ОС усилен и повторён тест
неудачного открытия: уже активный источник продолжает работать до результата
и сохраняется после ошибки. Последние уточнения касались теста, не EXE плеера.

## Новые сценарии Qt

`legacyOpeningKeepsUiResponsive` использует остановку worker перед реальным open,
настоящий UI event loop и IPC capabilities:

- admission возвращается без ожидания gate; второй legacy-open отклоняется;
- UI timer и capabilities отвечают, legacyPreparationPending=true;
- ошибка не заменяет действующий просмотр;
- Stop, Close media, закрытие окна, новый SDK start и уничтожение Window запрещают
  применение результата; уничтожение Window не делает join заблокированного worker;
- 5-секундный deadline выдаёт один результат отказа, слот остаётся занятым до
  завершения worker, затем освобождается;
- ошибка и отмена дают один сигнал завершения; после ошибки нет позднего запуска.

Отдельные реальные provider-тесты подтверждают обе дорожки, growing-prefix,
complete/failed/restart, lease release и сохранение очереди при ошибке источника.
Source regression Linux проверяет fragmented/indexed/derived-duration и локальный
HTTP download → provider → playback → merge; это собственный offline-стенд, не Harvester.
Опубликованный v2 writer по-прежнему играет >=10 секунд при физически неполных
файлах, затем complete/finished и подтверждённое закрытие без сетевых запросов.

## Classic: задержка реального открытия

Четыре случая `run_legacy_opening`: commit, Close, Stop, timeout. Windows oplock
удерживает первое открытие video.part. Тест проверяет событие oplock break,
runtime pending и три WM_NULL на случай. После подтверждения oplock успешный
запрос начинает playing, отменённые не начинают playing; provider leases пусты.
Всего **12 UI-pings**, максимум на стенде **0.2206 мс**. Это измерение конкретного
стенда, не гарантия времени ответа на любой системе.

Отдельный retained CLI/provider дошёл до position100ns=109725184 при границе
120000000 до complete, затем получил границу 200000000; после Close lease освобождён.
SDK regression повторяет start/probe/update/complete, leases/recovery/resource I/O,
v2 playback, async preparation/publication и registry/cleanup gates. SHA тестовой
копии совпадает со сборкой. INI заранее отключает контекстную регистрацию,
Formats не открывается; HKCU команды/иконки установки совпали до и после.
Одноразовые задачи удалены после тестов, LastTaskResult=0.

## Промежуточные результаты

Первый Linux API-прогон дал CTest exit 8: три сценария ожидали сигнал отказа
при отмене, которого ещё не было. Добавлен единичный progressiveOpeningFinished
для отмены; полные и финальные целевые повторения прошли. Source regression
в том прогоне уже прошёл (19 PASS).

Один параллельно запущенный Linux build завершился с 1: две сборки одного Ninja
каталога столкнулись на build.ninja (Text file busy). Последующие сборки выполнялись
последовательно, итоговые exit 0.

Первый Classic opening harness завершился с 1: он требовал attached-уведомление
после отмены. Это лишнее условие — отменённая подготовка может не подключаться
к provider. Финальный тест дожидается освобождения runtime pending-слота,
проверяет отсутствие playing и пустой реестр provider leases. Все четыре случая
и сохранённый legacy playback повторены с exit 0.

## Установка и оставшиеся границы

Linux EXE заменён атомарно, предыдущий сохранён для отката; просмотр не останавливался.
Версия плеера остаётся 0.2.0, расширение определяется capabilities и SHA-256.
INI до/после: `ac9387b8d09f63317ed8ea3b681abad96e5abe8890400d4db6540017e3acde75`.
SDK ZIP содержит исходные Python-модули/контракты/отчёты, а не новые установщики.

Полный F-01 ещё не закрыт: native I/O не прерывается принудительно; graph/renderer
shutdown, старые watch/refresh/notify locks и часть проверок CLI вне этого этапа.
Новые codec/live/remux-профили, реальные источники через Harvester и физическая
A/V-синхронизация здесь не квалифицированы. Windows-установки не заменялись.
