# RF64 sparse playback: отдельный согласуемый профиль

Новые container hints: `rf64-sparse-pcm16` и `rf64-sparse-pcm24`. Старые
`rf64-pcm16` / `rf64-pcm24` продолжают работать как prefix-gated profiles.
Поддержка нового режима согласуется через `standalone.rf64Pcm16.sparsePlayback`
и `rf64Pcm24.sparsePlayback`, с `version: 1`, `islandSeek: true`, явными
`profiles`, `container` и `knownLengthRequired: true`.

Профиль — один обязательный RF64 PCM audio resource с известной длиной,
неизменным проверенным head header, известной duration и нулевым offset.
PCM16 tag1 или PCM24 extensible PCM GUID, mono/stereo 44.1/48 kHz;
прежние RF64 bounds (16 GiB и 250000 packets) действуют одновременно.
Должны быть записаны header и первый полный PCM packet; обычный старт
по-прежнему ждёт 10 секунд head prefix либо полного короткого файла.

Виртуальные fMP4 offsets и временные метки всех PCM packets создаются из
проверенного RF64 header один раз. Заголовки moof/mdat не требуют чтения PCM.
Заполнение дыр не перенумеровывает packets, не сдвигает offsets и не меняет
tfdt/PTS. Это позволяет обоим splitter adapters находить целевой packet через
готовый индекс, сохраняя один pinned resource и reader.

Наличие геометрии не разрешает читать PCM из дыры. Для каждого payload read
проверяются writer-confirmed ranges; неподтверждённые bytes дают ожидание,
а при фактическом чтении подтверждённые bytes проходят SHA-256. Размер sparse
файла, его нулевое содержимое и готовые виртуальные заголовки не считаются PCM.
Stop/cancel/flush прерывают ожидание. Конец островка не является EOF.

Seek допускается только внутри непрерывного интервала целиком подтверждённых
PCM packets, с прежним небольшим запасом до его конца. Seek в дыру отклоняется.
После перехода ready end и буферизация вычисляются по интервалу у фактической
позиции, а не по первому head prefix. Действуют прежние 5 секунд resume buffer
и остановка при остатке меньше секунды; короткий хвост неполного файла может
ждать complete. Ручная пауза сохраняется при seek и writer updates.

В Qt каждый reload открывает свой immutable decoder window по FragmentIndex.
Audio-only libmpv может сигнализировать EOF при paused seek очень близко к концу,
что воспроизводится и на обычном завершённом WAV. Новый sparse profile использует
file-local [`keep-open`](https://mpv.io/manual/stable/#options-keep-open): ручная
пауза удерживает сеанс и фактическую позицию decoder. Контроллер завершает
подтверждённый EOF после снятия паузы. Никакая позиция не подставляется из запроса.
Отдельный Linux probe проверяет это с частным PulseAudio, а runtime-тесты
обеих Qt ОС — переход на паузе в последние 50 ms и последующее освобождение.
В Classic применяется штатный indexed DemuxSeek. Чтение за границей текущего
интервала ждёт публикации, не подставляет нули и не отменяет загрузку.
Можно перейти вперёд через дыру и обратно в head prefix. Если островок затем
расширен, ожидание снимается без смены reader.

В status `sparsePlayback: true`, `mediaMode: standalone-rf64-sparse-pcm16`
или `standalone-rf64-sparse-pcm24`; `availableSeconds` — ready end интервала
текущей позиции. Байтовые prefix/ranges diagnostics сохранены. `rf64RangeMap`
остаётся геометрической (`payloadVerified: false`), а её `islandSeek: true`
указывает на включённый отдельный playback profile. Карта до 32 интервалов
может быть усечена; внутренний seek использует все принятые ranges.

Публикации: до 128 ranges, 4096 SHA-блоков, до 16 MiB новых confirmed bytes
при открытии/update. Доступны inline и внешний SHA-index. Нужна явная
capability нового EXE; SDK отказывает старому плееру и неизвестной длине
до отправки запроса. Harvester не меняется и сам выбирает этот профиль.

Это ограниченный single-audio VOD режим. Другие контейнеры, video/B-frames,
live, startup без head prefix, codec/configuration changes и общая sparse
матрица остаются отдельными этапами. Большая синтетическая геометрия и
обычный физический >4 GiB prefix playback не заменяют отдельный физический
тест sparse island >4 GiB. Такой [дополнительный тест](ProgressiveLargeSparseRf64.md)
тех же SDK 1.81 EXE теперь проверяет PCM16/24 stereo 48 kHz во всех трёх редакциях;
другие большие rates/layouts и maximum-size/stress остаются открытыми.


Проверенный срез SDK 1.81: [результаты от 3 ноября](ProgressiveRf64SparseValidation-2026-11-03.json). Нативная матрица8 profiles × inline/external SHA, 272 checks на ОС; runtime 4 profiles каждой редакции, в том числе complete paused seek в последние 50 ms и отдельный cancel-at-gap. Предварительные18-групповые Qt-прогоны и окончательные8-групповые прогоны пересобранных бинарных файлов имеют отдельные hashes. Все итоговые exit codes 0.

[Дополнительные проверки пределов](ProgressiveRf64Limits.md) подтверждают восемь native upper-bound profiles на ОС и по два предельных playback profiles каждой редакции. 100 повторений относятся к native core; длительный playback stress и остальные варианты не считаются выполненными.
