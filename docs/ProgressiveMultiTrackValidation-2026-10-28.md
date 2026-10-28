# Проверка нескольких аудио и SRT, 28 октября 2026

SDK 1.22, три отдельные редакции. [Контракт и ограничения](ProgressiveMultiTrack.md).
[Полные результаты, состояния и SHA256](ProgressiveMultiTrackValidation-2026-10-28.json).
Harvester и установленные Windows EXE/профили не изменялись.

| Стенд | Результат |
|---|---|
| Linux Qt | 8 CTest групп; API aggregate 123 PASS, legacy/source 19 PASS, 50 прежних медиавариантов + multi-track; exit 0 |
| Windows Qt | 7 CTest групп; API 124 PASS, 50 медиавариантов + multi-track; exit 0 |
| Windows Classic | 9 SDK контрактов, 50 медиавариантов, отдельный multi-track и legacy/provider; exit 0 |
| Native media | 453 PASS обычным runner и ASan/UBSan с leak detection; exit 0 |
| Python SDK/family | 29 PASS; exit 0 |
| CMake install | Новые контракт и отчёты включены; exit 0 |

Полные матрицы проверены на сборках с основным multi-audio/seek механизмом.
Последнее изменение Qt ограничивает дальний переход для неполных источников и
использует byte seek для готовых. На поставляемых EXE отдельно повторён усиленный
сценарий: сохранение aid/sid и реальное Vorbis decoding после seek, явный отказ
при target=11 до complete и успешный переход к 19 секундам после complete.
Exit 0 на Linux Qt и Windows Qt; SHA256 обеих стадий сохранены в JSON.

Сценарий: физически недокачанные H.264 fMP4 + AAC fMP4 + Opus WebM + Vorbis
WebM; переключение всех трёх аудио, перемотка назад, ранний и поздний русский
UTF-8 SRT, дописывание/переименование закреплённых ресурсов, естественное завершение
и квитанции освобождения. Qt дополнительно проверяет реальный decoder audio codec,
названия меню, поздний русский sub-text и сохранение aid/sid с последующим
Vorbis decoding после seek без повторного выбора дорожек. Classic проверяет фактические выбранные
индексы AudioSwitcher и два подключённых SubtitleStream; физический звук и пиксели
субтитров этим стендом не измеряются.

Исправлены потеря старого packet cache libmpv после переключения audio и
преждевременный Release borrowed SubtitleStream Classic. Первоначальные ошибки
и точные exit codes сохранены в JSON. В общем слое проверены пределы дорожек,
недопустимые codec hints/default ordering/required flags, malformed UTF-8/SRT,
приватные копии с BOM, позднее завершение и удаление копий/квитанций.

Linux обновлён атомарно; 95 существующих личных файлов
сверены, INI SHA256 до/после `ee2a51243880e578a7b1f38a0278ed178d1e2e379ba0f4f756b5f77427ac24b6`.
Резервная копия: `foxicmp-qt.before-sdk-1.22-20261028T161942Z`. Версия приложения остаётся 0.2.0,
возможности API определяются capabilities. Установщики 0.2.0 этим SDK не заменены.

Пределы Qt: при нескольких аудио неполный источник допускает переходы к
позициям 0–10 секунд; после готовности A/V — byte seek по всей длительности.
Classic работает со своим индексом. Полная дальняя перемотка Qt при загрузке
ещё не реализована. Остальные пределы: все A/V required и готовы к start; до четырёх optional полных SRT,
только UTF-8/VOD/нулевой timestamp offset. Optional audio, ASS/bitmap/embedded
subtitles, главы/общие metadata, HLS/DASH timeline, live и muxed/remux ещё требуют
работы. Граница восьми audio проверена native контрактом; реальный
сквозной стенд использует 3 audio/2 SRT. Предел четырёх SRT объявлен контрактом. Оборудование и Wayland исключены.
