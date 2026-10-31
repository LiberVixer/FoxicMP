# FoxicMP: чеклист завершения progressive-ТЗ

Основание: [ТЗ от 25 октября 2026](ProgressiveTechnicalSpec-2026-10-25.ru.md),
[история реализации](ProgressiveIntegrationPlan.md), [семейный план](FoxicMPFamilyPlan.md).
Текущий проверенный срез — SDK 1.50, приложение 0.2.0. Этот документ — перечень
работ и критериев; пустой checkbox не означает отсутствие функции у обычного
воспроизведения завершённых файлов. Проверяется именно progressive-сценарий.

Статусы: `[x]` — выполнен указанный узкий результат с доказательством;
`[ ]` — остаётся открытым. Начатый пункт отмечается текстом, без преждевременного `[x]`.
Все применимые пункты проверяются отдельно в Linux Qt, Windows Qt и Classic.

## Проверенная основа

- [x] Совместимые v1 start/update/complete и исправление sessionDirectory Classic — [проверки](ProgressiveV1Validation-2026-10-25.md).
- [x] Общий SDK/client, OS discovery/IPC и Windows Qt legacy/provider adapter в ограниченном профиле — [проверки](ProgressiveSdkValidation-2026-10-25.md).
- [x] Модель v2, подтверждённые ranges, resource I/O и reader receipts — [контракт](ProgressiveResourceIo.md).
- [x] Объявленные SHA-256 blocks и проверка перед чтением — [контракт и границы](ProgressiveIntegrity.md).
- [x] Проверенные ограниченные separate/segmented fMP4/WebM/TS профили — [матрица](FoxicMPFamilyMatrix.md).
- [x] Shared growing AVC/AAC fMP4 и AVC/AAC TS — [fMP4](ProgressiveMuxedFmp4.md), [TS](ProgressiveGrowingTs.md).
- [x] Shared growing WebM VP8/VP9/AV1 с Opus/Vorbis — [SDK 1.46 и проверки](ProgressiveMuxedWebmVorbisValidation-2026-10-31.md).
- [x] Ограниченные multi-audio, поздние полные UTF-8 SRT, metadata/chapters и optional track isolation — [история](ProgressiveIntegrationPlan.md).

## 1. Матрица общего растущего fMP4 — завершён проверенный ограниченный профиль

- [x] HEVC Main/Main10 + AAC: 8/10-bit, hvc1/hev1, B-frames, fractional fps, edit lists, partial fragments — [SDK 1.47 и проверки трёх редакций](ProgressiveMuxedFmp4HevcValidation-2026-10-31.md).
- [x] AV1 Main 8/10-bit + AAC: fractional fps, edit lists, partial fragments, SHA/leases — [SDK 1.48 и проверки трёх редакций](ProgressiveMuxedFmp4Av1Validation-2026-10-31.md).
- [x] VP9 Profile 0/2 8/10-bit + AAC: fractional fps, edit lists, partial fragments, SHA/leases — [SDK 1.49 и проверки трёх редакций](ProgressiveMuxedFmp4Vp9Validation-2026-10-31.md).
- [x] H.264/HEVC/VP9/AV1 + Opus mono/stereo 48 kHz, 8/10-bit и fractional profiles — [SDK 1.50 и проверки трёх редакций](ProgressiveMuxedFmp4OpusValidation-2026-10-31.md).
- [x] Для каждой пары: explicit capability, native bytes/clock/decoded hashes и playback/buffer/pause/seek/complete/release трёх редакций — отчёты SDK 1.42, 1.47–1.50 выше.

## 2. Общий demux и локальная подготовка

- [ ] Проверенные backend demuxers через управляемый local I/O; платформенные адаптеры Qt/Classic.
- [ ] Обычные MP4/M4A/MOV, WebM/Matroska, MPEG-TS и их допустимые варианты вне нынешних узких профилей.
- [ ] Явные native-progressive, progressive-remux, waiting, post-download и unsupported результаты.
- [ ] Remux только подтверждённых локальных данных, без перекодирования/ухудшения качества и без изменения оригиналов.

## 3. Обычный MP4 и индексы

- [ ] Fast-start и обычные sample tables.
- [ ] Moov в конце: ожидание индекса и согласуемый запрос приоритетного диапазона writer.
- [ ] Старт до complete при наличии индекса/данных; отсутствие индекса не маскируется успешным fallback.
- [ ] Корректный обычный seek после complete, при необходимости контролируемое reopen с сохранением состояния.

## 4. Аудио и самостоятельные потоки

- [ ] Полная согласованная audio codec matrix: AAC-LC/HE-AAC, Opus, Vorbis, MP3, FLAC, PCM, AC-3/E-AC-3.
- [ ] ADTS AAC, MP3, Ogg, FLAC, WAV/PCM; дополнительные rates/layouts и многоканальный звук.
- [ ] Progressive audio-only и video-only без ожидания несуществующего второго потока.
- [ ] Отсутствие аудиоустройства не делает валидный video-only источник ошибочным.
- [ ] MPEG-2/MPEG-4 Part 2 для предусмотренных ТЗ загрузок/локальных входов.

## 5. Непоследовательная запись и большие ресурсы

- [ ] Карта ranges в оставшихся playback-режимах: sparse holes, out-of-order запись и доступные островки данных.
- [ ] Незаписанные дыры не читаются как нули; file size не заменяет подтверждение writer.
- [ ] Масштабируемые media indices и внешний индекс digest без огромного командного JSON.
- [ ] Быстрый поиск/старт большого каталога без полного предварительного сканирования.

## 6. Время и буферизация

- [ ] VFR, разные timebase, ненулевые/отрицательные offsets, preroll, edit lists и gapless metadata во всей матрице.
- [ ] Нужные варианты TS access units, discontinuity и rollover транспортных часов.
- [ ] Доступная граница по обязательным выбранным потокам; исходные PTS/DTS не обнуляются независимо.
- [ ] Start/resume/buffer policies, ручная пауза и восстановление синхронизации после ожидания/seek.

## 7. Конечные HLS/DASH

- [ ] Локальные init/media segments, параллельное получение, gaps, discontinuity и rebasing.
- [ ] Расширенная сегментная codec/container matrix, включая Vorbis где применимо.
- [ ] Состояние аудио между сегментами, смена init/config и безопасный доступ к ключевым кадрам.
- [ ] Длинные каталоги, ограниченные handles/cache и повторное открытие ресурсов.

## 8. Смена качества/configuration

- [ ] Контролируемая переинициализация decoder на подходящей точке декодирования.
- [ ] Сохранение позиции, pause, speed, volume и выбранных дорожек.
- [ ] Недостаточные данные новой конфигурации дают ожидание с точной причиной.

## 9. Динамические аудиодорожки

- [ ] Несколько audio в muxed и segmented источниках; динамическое добавление/отключение.
- [ ] Стабильные ID, language/label/default/forced и сохранение выбора.
- [ ] Невыбранные optional tracks не тормозят просмотр; выбранная недокачанная дорожка ждёт свои данные.
- [ ] Ошибка optional audio изолируется и объясняется; язык не подменяется молча.

## 10. Субтитры

- [ ] Растущие SRT/WebVTT/ASS/SSA/TTML, адаптеры JSON3/SRV.
- [ ] UTF-8/UTF-16/BOM и неполные cues; только завершённые события без дубликатов.
- [ ] Embedded mov_text/WebVTT/ASS/SubRip; PGS/DVD subtitles с нужными headers/palettes в локальных контейнерах.
- [ ] Styles/settings/language/default/forced; ограничения конвертации описаны без ложного обещания полного оформления.
- [ ] Безопасные bounded fonts/attachments без установки в систему и внешних XML/HTML ресурсов.

## 11. Главы и metadata

- [ ] Совместная обработка container/writer chapters, стабильные IDs, late updates без сброса позиции.
- [ ] Неизвестная финальная duration и главы за доступной границей.
- [ ] Covers/preview, названия дорожек, rotation/aspect ratio и HDR/color metadata.

## 12. Live

- [ ] Live/event HLS и dynamic DASH через локальную публикацию writer.
- [ ] Неизвестная duration, движущееся окно, отдельная целевая задержка и reconnect.
- [ ] Отсутствие сегмента/heartbeat не означает EOF; конец подтверждает writer.
- [ ] Retirement вышедших ресурсов только после освобождения всеми readers; seek за окном имеет отдельную причину.

## 13. Жизненный цикл, I/O и безопасность

- [ ] Оставшиеся UI-blocking пути probe/index/remux/read/teardown, Qt GL cleanup и обычные Classic graphs.
- [ ] Управляемая отмена ожиданий, bounded остановка внешнего процесса, фактические release/filesClosed receipts.
- [ ] Writer/player crash, resume по идентичности bytes, несколько readers, rename и переход на итоговый файл.
- [ ] Ошибка preview не отменяет download; merge/writer errors не становятся успешным EOF.
- [ ] Long paths, включая снятие нынешнего 240-символьного прикладного ограничения Classic.
- [ ] Owner/permissions, reparse/symlink/path traversal/substitution, IPC current-user, секреты не попадают в логи.
- [ ] Network/removable filesystem capabilities только по отдельным доказательствам; обязательная база — local fixed drive.

## 14. API, UI и диагностика

- [ ] Общие точные reasonCodes и states для всех новых режимов; ok принятия команды не означает playing.
- [ ] Название/источник/выбранные дорожки/доступная граница/буфер/причина ожидания.
- [ ] Pause/Stop/Seek, главы и track menus остаются отзывчивыми; activate=false не перехватывает фокус.
- [ ] Capabilities отражают фактические EXE/decoder builds; v1/client/legacy adapters не регрессировали.

## 15. Приёмка плеера

- [ ] Общая positive/negative matrix трёх редакций, минимум 10 секунд просмотра до complete.
- [ ] Неравномерная запись, writer pauses 10/30/60 s, partial structures, holes, ошибки, disk full, cancel/crash.
- [ ] Реальные разрешённые YT/VK/Rutube direct/HLS/DASH сценарии с protocol/format IDs и offline fixtures без секретных URL.
- [ ] Длительные загрузки, минимум 100 start/cancel циклов, несколько readers, RAM/cache/temp/CPU/handles/threads без нарастающих утечек.
- [ ] Обычные завершённые файлы, пользовательские профили и shell registration не регрессировали.

## 16. Зависимость реального Harvester

- [ ] Отдельное разрешение/задача на подключение реальных download events и кнопки preview в Harvester.
- [ ] Сквозные start/update/complete/error/cancel/release из реального Harvester во всех трёх редакциях.
- [ ] До этого допустим статус «плеер готов к интеграции», но не «всё ТЗ выполнено».

Действующий запрет изменять Harvester сохраняется. Этот чеклист не является
разрешением менять другой проект.

## 17. Итоговая поставка

- [ ] Три проверенные сборки, Windows EXE/MSI и Linux .deb, SDK и инструкции интеграции.
- [ ] Исходники, воспроизводимые fixtures, версии/build options/licenses компонентов и итоговая matrix с exit codes.
- [ ] Независимые установки/профили/регистрация; Linux обновлён с backup и проверкой сохранности INI.
- [ ] Итоговый GitHub release и проверка скачанных assets/checksums/source commit.

## Численные критерии ТЗ

- [ ] Старт после готового буфера ≤3 s, resume ≤2 s на согласованном эталонном ПК; remux preparation измеряется отдельно.
- [ ] Абсолютная A/V ошибка ≤80 ms; накопленный drift за 30 min ≤40 ms, с описанной методикой физического выхода.
- [ ] Stop/Close/Cancel освобождает управляемые ожидания ≤2 s; UI не зависает.
- [ ] Все обязательные positive rows реально играют до complete; нет чтения неопубликованного, ложного EOF, порчи исходников и потери выбора/metadata.
- [ ] Необъяснимые SKIP и неподтверждённые OS/backend результаты не считаются PASS.

Если физический A/V стенд недоступен, приёмка этого пункта остаётся открытой.
Отложенные пользователем дисковые меню, аналоговое ТВ, физические устройства и
Wayland этим чеклистом не возобновляются. Промежуточные feature-релизы не закрывают
всю матрицу. Каждый новый формат проверяется в допустимых для контейнера сочетаниях.
