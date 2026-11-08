# Windows TEMP: manifest control and Classic registry selection

Продолжение 8 ноября: [второе ожидание графа локализовано и ограниченный
Classic long-TEMP playback срез прошёл](ProgressiveClassicLongTempPlayback.md).
Ниже сохранены исходные частичные результаты этого этапа.

8 ноября 2026. SDK 1.84 / приложение 0.2.0. Статус — частичное исправление:
Classic больше не вызывает блокирующий GetTempPathW при выборе reader registry.
Воспроизведение Classic при TEMP320/1100 остаётся открытым.
[Точные результаты](ProgressiveWindowsTempManifestValidation-2026-11-08.json).

## Почему старый probe не описывал Classic

Classic и Python 3.11 имеют longPathAware=true в PE manifest. У native reader
helper и Qt main этот opt-in отсутствовал. В прежних отчётах измеренный короткий
fallback относится к конкретным probe EXE; переносить его на Classic нельзя.

Для контроля собраны два native EXE из одного C++-исходника: обычный и
[с longPathAware manifest](../tests/windows/runtime_paths_aware.manifest).
На этой машине:

| Запрошенный TEMP | Без opt-in | longPathAware=true |
|---|---|---|
| 120 / 240 | GetTempPathW вернулся, root совпал | GetTempPathW вернулся, root совпал |
| 320 / 1100 | GetTempPathW/GetTempPath2W выбрали короткий fallback | GetTempPathW не вернулся за 5 секунд |

Последний контроль ставит stderr-маркер **после** выделения буфера, непосредственно
перед вызовом API. До таймаута есть before-GetTempPathW, но нет after-GetTempPathW.
GetTempPath2W в этих aware-случаях не достигнут: его поведение отдельно не доказано.
Deadline завершает только owned child; forced cleanup exit 1 не считается
самостоятельным crash. Collector exit 0 означает сбор наблюдений, а не PASS
двух строк с timeout 124. Это локализация на проверенном хосте, не утверждение
о каждой Windows или о причине зависания родительского Python в CreateProcess.

## Исправление Classic

[WindowsTemporaryDirectory](../shared/progressivetemppath_win.h) выбирает TMP,
затем TEMP, USERPROFILE и Windows directory; нормализует путь динамическим
GetFullPathNameW с проверкой длины и bounded retry при изменении environment.
Такой порядок соответствует [документированной очередности GetTempPathW](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-gettemppathw).
Существование и доступность папки по-прежнему проверяются при registry I/O;
owner/ACL/reparse guards, interprocess locking и no-overwrite не менялись.

Это локальный resolver progressive runtime Classic. Он не подменяет WinAPI,
не отключает longPathAware, не меняет environment процесса, Windows policy,
имена endpoint, профиль, defaults или DLL других приложений. Логика Qt не менялась.

Проверки resolver: 8 сочетаний root120/240/320/1100 × два manifest и 3 fallback
приоритета, native/Python exit 0. Конфликтующий TEMP не перекрывает TMP.
Новый CTest progressive-temp-directory также прошёл, exit 0; его report и
рабочие папки частные и удаляются. Native reader conformance: обычный/320/1100
для обоих Windows EXE — 6 запусков по 13 checks, все exit 0.

## Что подтверждено в настоящем Classic

Старый private Classic с тем же trackReader сценарием при TEMP320 вновь
не ответил на start за стандартные 5 секунд, exit стенда 1.
Кандидат с новым resolver прошёл четыре indexed/fragmented growing playback
случая при TEMP120/240: position>0.2 до complete, append/complete, cancel,
явные released/filesClosed, forget и два normal main exit 0. Исходные bytes
сверены. Приватная MSVC сборка ClassicProgressiveApi и link завершились 0;
остальные objects/libs взяты из ранее проверенной private сборки. Это не новая
полная clean-release сборка семейства.

При TEMP320 и TEMP1100 start кандидата уже принят, registry действительно
расположен в запрошенном длинном root: после выхода процесса его persisted
receipt прочитан независимым native helper при cleanup.
Наблюдение TEMP1100: start около 15 ms, 292 heartbeat/status replies за 15 s,
самый долгий около 16 ms. Это диагностика одного ПК, не общая latency приёмка.
API остаётся отзывчивым, но source state — buffering, position=0,
availableSeconds=0. Воспроизведение не засчитано, оба длинных прогона имеют exit 1.

После ошибки плеер принудительно закрыт с ожиданием ОС; recovered receipt
reason=process-exited, released=true, **filesClosed=false**. Это подтверждение
выхода владельца для cleanup, не явная квитанция закрытия живого reader.
Физический A/V, normal long-root EOF и общий shutdown budget не подтверждены.
Следующий шаг — наблюдение открытия графа/источника и поиск следующего ожидания;
подмена renderer или отключение manifest в продукте не применялись.

## Изоляция и оставшиеся работы

ContextMenuPrompted=1 и ContextMenuEnabled=0 записаны до запуска каждой Classic
копии. 583 protected SHA и 158 HKCU entries совпали до/после. Два one-shot task
удалены, соответствующих процессов и точных owned test roots не осталось.
Windows-установки, Linux main/INI, опубликованные assets и Harvester сохранены.
Linux-код приложения не изменён, переустановка ему не требовалась.

Открыты Classic long-TEMP playback, точный Windows Qt TEMP320/1100 вместо
системного fallback, внешний Python launcher и остальные пункты общего ТЗ.
Исторические FAIL и прежние SHA не заменены новыми результатами.
