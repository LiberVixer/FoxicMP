# Проверка legacy maintenance — 26 октября 2026

[SDK 1.11](ProgressiveLegacyMaintenance.md), базовый commit
`6499887db40c92e470e9620114ebc02135d4686e`.
[Машинный отчёт с SHA-256](ProgressiveLegacyMaintenanceValidation-2026-10-26.json).
Harvester и установленные Windows EXE/профили не изменялись.

## Итоги

| Стенд | Финальная проверка | Exit |
|---|---|---:|
| Linux Qt | Полный API 63 PASS, maintenance-группа 9, legacy source regression 19 | 0 |
| Windows Qt | Полный API 64 PASS, maintenance-группа 10, реальный v2 playback 3 | 0 |
| Windows Classic | Три реальных legacy maintenance случая, полный SDK/reader/resource/v2/preparation/publication regression и отдельный retained provider playback | 0 |
| Python SDK / семейный контракт | 35 / 2 теста | 0 |

Числа QtTest включают init/cleanup. Maintenance-группа — пять новых сценариев,
дополненных обычным provider complete/failed и Windows provider restart.
Linux UI/audio использует частные X11/D-Bus/PulseAudio, Windows — отдельные профили.

## Управляемое ожидание Qt

Worker остановлен после чтения новой ревизии, до проверки файлов/доиндексации.
Hook хранится через atomic shared_ptr, его установка не гоняется с watch-потоком.
Проверены:

- UI timer, IPC capabilities и metadata notify продолжают отвечать;
- snapshot revision и available остаются прежними во время задержки;
- после снятия gate новая revision и доступная граница видны вместе, начинается
  реальное декодирование обеих дорожек;
- Stop и Close во время задержки возвращаются без ожидания gate, не отдают
  положительный local close receipt, пока тест удерживает настоящий Source;
- после отмены нет позднего commit и новой ошибки повреждения из-за отменённого чтения;
- усечение проверяется при новой и при неизменной revision, появляется ошибка,
  не successful EOF/complete; затем реальные дескрипторы закрываются.

Общий regression дополнительно подтверждает growing-prefix, отмену, provider
restart, полный v2 writer, indexed/fragmented MP4, queue preservation и локальный
HTTP download/provider/merge стенд. Это собственные фикстуры, не сквозной Harvester.

## Граница подтверждения старого Unix provider

При усечении старый Unix provider валидирует физический размер файлов прежде,
чем принимает `released`. Поэтому он может отвергнуть повреждённую сессию и
сохранить process lease даже после действительного закрытия файлов плеером.
Linux-тест в обоих truncate-случаях проверяет **local files closed и сохранённый
provider receipt**. Квитанция не очищается искусственно, timeout не выдаёт ACK.
Загрузчик должен сохранить файлы и использовать подтверждённый OS/PID recovery;
эта проверка не выдаётся за успешное provider acknowledgement при живом процессе.
Обычные grow/Stop/Close и Windows provider подтверждают пустой реестр leases.

## Classic: реальная доиндексация

Offline fixture добавляет 100000 корректных free atoms перед новыми video moof/mdat
и атомарно публикует revision 2. В grow/Stop случаях до UI-pings зафиксированы
**820 / 624** дополнительных ReadOperations процесса; provider ещё сообщает revision 1.
Все шесть WM_NULL отвечают, максимум на стенде **0.2487 мс**. Счётчик относится ко
всему процессу; не утверждается атрибуция каждой операции единственному worker.
Provider revision не является отдельным измерением внутренней UI snapshot revision;
согласованный Classic commit дополнительно проверен по структуре критической секции.

После обычной доиндексации readyEnd=120000000 и revision=2. Stop во время чтения
оставляет прежний readyEnd=50000000/revision=1 и освобождает provider lease без новой
ошибки повреждения. Усечение при неизменном manifest обнаружено как error,
после закрытия provider leases пусты.

Обнаружено и исправлено отключение команды Stop при MLS_LOADING: обычное состояние
DirectShow ещё недоступно, но progressive-сеанс уже создан. Меню/toolbar теперь
разрешают Stop в этом состоянии; обработчик отмены и темы настроек сохранены.

Retained CLI/provider отдельно дошёл до position100ns=100106373 при границе
120000000 до complete, после complete — граница 200000000, Close и lease release.
Полный SDK regression повторён на том же финальном EXE. SHA тестовых копий совпал
со сборкой. INI заранее отключал регистрацию, Formats не открывалась. Команды/иконки
установленного Classic в HKCU совпали до/после. Одноразовые задачи удалены,
LastTaskResult=0; рабочий стол и микшер пользователя не перенастраивались.

## Промежуточные ошибки

Первый Qt maintenance-тест дал CTest exit 8: жёстко ожидал revision=1, хотя init
фикстуры уже публикует revision=2. Ожидания привязаны к фактической стартовой ревизии.
Первый Classic-прогон завершился с 1: Stop не включался во время initial graph
loading. Исправлено разрешение команды; все три native случая повторены успешно.
Следующий Linux-прогон дал exit 8 из-за требования пустых provider leases в двух
truncate-случаях (61 PASS / 2 FAIL). Тест теперь различает закрытие локальных
дескрипторов и консервативный provider receipt, как описано выше; финальные все PASS.

## Установка и ограничения

Linux EXE заменён атомарно, прежний сохранён для отката; просмотр не прерывался.
Версия 0.2.0, расширения определяются capabilities и SHA-256.
INI до/после: `ac9387b8d09f63317ed8ea3b681abad96e5abe8890400d4db6540017e3acde75`.

Native I/O не отменяется принудительно. Обслуживание файлов и heartbeat legacy
по-прежнему используют один worker: зависший системный вызов может задержать его
работу, хотя metadata mutex UI больше не удерживается этим I/O. Graph/renderer
shutdown, CPU/RAM больших индексов, полная integrity/codec/live/remux-матрица,
реальный Harvester и физические A/V-измерения этим этапом не закрываются.
SDK ZIP не меняет публичные установщики 0.2.0.
