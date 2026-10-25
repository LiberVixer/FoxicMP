# Расширенная модель сессии: проверка трёх редакций

25 октября 2026. [Результаты и SHA-256](ProgressiveModelValidation-2026-10-25.json).
Это основа этапа 3 [плана](ProgressiveIntegrationPlan.md), а не приёмка всего ТЗ.
[Черновой контракт v2](ProgressiveManifestV2.md), [SDK 1.3](ProgressiveSdk.md).

## Проверено

| Проверка | Exit code | Результат |
|---|---|---|
| Linux Qt, сборка | 0 | Приложение, model и API tests |
| Windows Qt, native MinGW build | 0 | Приложение и тесты |
| Classic, DSUtil/BaseSplitter/MP4Splitter/application | 0 | MSVC x64 |
| Общая модель: GCC, MinGW, MSVC | 0 каждый | По 71 вектору и отдельный raw duplicate-key сценарий |
| Общая модель: AddressSanitizer + UndefinedBehaviorSanitizer | 0 | 71 вектор и raw duplicate-key; detect_leaks=1 |
| Linux Qt, private X11/D-Bus/PulseAudio | 0 | Model и API: 2 группы; API 16 passed, 0 failed/skipped; 99,847 с |
| Windows Qt, private profile/offscreen | 0 | Model и API: 2 группы; API 17 passed, 0 failed/skipped; 95,320 с |
| Classic, отдельная копия и одноразовая задача | 0 | 62 проверки validate через API, 100 RPC, cold SDK/writer, штатный exit 0 |
| Python SDK | 0 | 7 тестов |
| Семейный контракт | 0 | 2 теста |

71 вектор модели охватывает uint64 больше 2^53 и его максимум, int64 minimum,
неправильные числовые типы, лимиты и переполнение, дыры/пересечения диапазонов,
неизвестные ссылки, muxed/audio-only/video-only метаданные, сегменты и codec
configuration, поздние главы, stale revision, terminal-переходы и reader lifecycle.
Из него 62 проверки структуры/обновления применимы к API всех трёх редакций;
оставшиеся проверяют внутренние covers/ReaderBook без публикации их как API.

Qt проверяет validate при действующем v1-источнике и подтверждает, что он остаётся
тем же объектом. Start/probe v2 отклоняются как unsupported_schema. В полном
Qt API regression сохранены indexed/fMP4 sessionDirectory, настоящий playing до
complete, SDK/release и legacy/provider; Windows Qt также проверяет reconnect.

Classic writer достиг позиции 12,8453986 с до complete; стенд наблюдал playing
10,437 с до complete. Затем finished, active=false, released=true. Это показания
плеера, не измерение физического A/V. Оба входа локальные, сетевых запросов 0.
Контекстная регистрация установленного Classic в HKCU до/после совпала. Тестовая
копия создана с ContextMenuPrompted=1, ContextMenuEnabled=0; страницы настроек не
применялись. Одноразовая задача удалена с LastTaskResult=0.

Первый вызов Linux wrapper завершился exit 2 до тестов: извлечённый Xvfb отсутствовал
в PATH. Повторный запуск с каталогом тестовых инструментов прошёл. Системные пакеты,
микшер и рабочий сеанс пользователя не менялись.

## Поставка и границы

Linux EXE заменён атомарно, прежний сохранён в резервной копии; просмотр не
останавливался. INI до/после имеет SHA-256
`ac9387b8d09f63317ed8ea3b681abad96e5abe8890400d4db6540017e3acde75`.
SDK и документация обновлены в integration. Версия приложения остаётся 0.2.0;
различать эту сборку следует по capabilities и SHA-256. Рабочие Windows EXE/INI
и установщики 0.2.0 не заменялись. ZIP SDK не содержит бинарников плеера.

Сейчас playbackSchemaVersions=[1], validationSchemaVersions=[2]. Схема 2 проверяет
метаданные, не открывает файлы и не подтверждает поддержку кодеков. ReaderBook —
in-memory модель: runtime/IPC, дескрипторы, долговечный межпроцессный registry и
восстановление после аварии ещё не подключены. Компактизация ресурсов live-окна
запрещена до этой интеграции. Управляемое I/O, demux/remux и расширенная матрица
форматов остаются следующими этапами. YouTubeHarvester не изменён; сквозная проверка
из него, физическая A/V точность и длительный дрейф здесь не выполнялись.
