# Проверка mixed MP4/WebM, 28 октября 2026

SDK 1.20. Три отдельные редакции; YouTubeHarvester и установленные Windows
EXE/профили не изменялись. [Контракт и ограничения](ProgressiveMixedContainers.md).
[Полные результаты, состояния, hashes и exit codes](ProgressiveMixedValidation-2026-10-28.json).

## Матрица

| Стенд | Результат |
|---|---|
| Linux Qt | 3 CTest группы, API 96 PASS, legacy/source 19 PASS; exit 0 |
| Windows Qt | 2 CTest группы, API 97 PASS; exit 0 |
| Windows Classic | 8 SDK контрактов, 34 медиаварианта, отдельный mixed H.264/Opus и legacy provider; exit 0 |
| Native media | 291 PASS обычным runner и ASan/UBSan с leak detection; exit 0 |
| Выбор каталога фикстур через FOXICMP_TEST_MEDIA | 291 PASS, exit 0 |
| Python SDK/family | 29 PASS, exit 0 |

Qt totals включают init/cleanup, пропусков нет. Каждая редакция отдельно
проверила все 34 медиаварианта: 19 прежних и 15 смешанных. Из них семь —
fMP4-видео H.264/HEVC/AV1/VP9 + WebM Opus, восемь — WebM-видео VP9/AV1
8/10-bit + fMP4 AAC-LC/Opus. Новые аудиофикстуры — stereo 48 kHz.

## Сквозной сценарий

Начало с двух физически неполных файлов, публикация подтверждённых диапазонов,
не менее 10 с воспроизведения до complete; у всех смешанных вариантов
наблюдалась перемотка назад до завершения загрузки. Затем переименование
удерживаемых файлов, дописывание, complete, естественный finished, filesClosed
и удаление reader receipts. Повторный запуск и ошибки writer/reader identity
проверены общим сценарием. Native-корпус проверяет оба заголовка, отсутствие
codecHint, ошибочные containerHint, чтение границы и отмену.

Qt дополнительно подтвердил меняющиеся декодированные видеокадры, нужный
8/10-bit pixel format декодера и аудиокодек/48 kHz. Classic проверен отдельно
через DirectShow; чтение двух дорожек и движение часов не выдаются за
физическое измерение синхронизации звука и изображения.

Classic работал в отдельной копии с ContextMenuPrompted=1 и
ContextMenuEnabled=0; до/после проверена неизменность HKCU команд и иконок
установленной версии. Хеш тестовой копии совпал со сборкой. Одноразовая задача
тестирования удалена, LastTaskResult=0.

## Установка Linux

EXE заменён атомарно, прежний сохранён для отката:
`_bin/FoxicMPQt-linux-x64/foxicmp-qt.before-sdk-1.20-20261028T050544Z`.
Профиль и 92 существующих личных/служебных файлов сохранены.
Версия приложения остаётся 0.2.0; новые возможности определяются capabilities.

| Файл | SHA-256 |
|---|---|
| Linux Qt | `18e8d284a4d47a606fa960c3b7802fe9a8f1b613ae525452d033a0962780731e` |
| Windows Qt | `847e0da01b439cea1634604b661e8d3c9c780e2e44650bfd73e430530e4160bf` |
| Windows Classic | `2507462e2ed1c26bb7d42952f5f385537846e2344978ce932eed22ea3cb78e33` |
| Linux INI до и после | `ee2a51243880e578a7b1f38a0278ed178d1e2e379ba0f4f756b5f77427ac24b6` |

VP8/Vorbis, muxed/remux, live, дополнительные дорожки/metadata, реальные
источники Harvester, физическое A/V и длительный дрейф этим этапом не закрыты.
Оборудование и Wayland не проверялись по решению пользователя.
SDK ZIP не обновляет опубликованные установщики 0.2.0.
