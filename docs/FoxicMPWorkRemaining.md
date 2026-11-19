# FoxicMP: что ещё нужно сделать

Из 537 конечных пунктов готово 312, не завершено 225.
Активный план: 200; внешние условия: 13; отложено: 12.
Общие родители здесь служат навигацией; они не добавляются к числу конечных пунктов.
[Полный счётчик и выполненные работы](FoxicMPWorkChecklist.md).

«Внешнее условие» и «отложено» не разрешают автоматически менять Harvester,
проводить аппаратные испытания или возвращаться к исключённому Wayland.

**Следующий конкретный пункт очереди:** P02.C149.04 — WebM VP8/AV1: continuous пути за пределами native профилей.

## P02. Demux и непрерывный remux контейнеров

**P02.C148. Проверенные backend demuxers через управляемый local I/O; платформенные адаптеры Qt/Classic**

- **P02.C148.01** — осталось: Подключить production demux через confirmed local I/O для ещё не поддержанных входов. [Основание](ProgressiveCompletionChecklist.md).
- **P02.C148.02** — осталось: Подключить оставшиеся Qt/Classic decoder adapters без открытия частичного файла как обычного. [Основание](ProgressiveCompletionChecklist.md).
- **P02.C148.03** — осталось: Проверить limits/cancel/malformed/EOF для каждого вновь подключённого demux path. [Основание](ProgressiveCompletionChecklist.md).

**P02.C149. Обычные MP4/M4A/MOV, WebM/Matroska, MPEG-TS и их допустимые варианты вне нынешних узких профилей**

- **P02.C149.04** — осталось: WebM VP8/AV1: continuous пути за пределами native профилей. [Основание](ProgressiveCompletionChecklist.md).
- **P02.C149.05** — осталось: WebM/Vorbis: continuous путь за пределами native профиля. [Основание](ProgressiveCompletionChecklist.md).
- **P02.C149.06** — осталось: Matroska H.264/HEVC и допустимые пары аудио. [Основание](ProgressiveCompletionChecklist.md).
- **P02.C149.07** — осталось: Matroska: lacing/CodecPrivate/Cues и track identity за пределами WebM. [Основание](ProgressiveCompletionChecklist.md).
- **P02.C149.08** — осталось: MPEG-TS: continuous remux HEVC и предусмотренных MPEG-вариантов. [Основание](ProgressiveCompletionChecklist.md).
- **P02.C149.09** — осталось: Separate/multi-input remux и обязательные ещё не покрытые muxed сочетания. [Основание](ProgressiveCompletionChecklist.md).
- **P02.C149.10** — осталось: Иные допустимые MP4/WebM layouts за пределами опубликованных строгих профилей; перечень заморозить до реализации. [Основание](ProgressiveCompletionChecklist.md).

**P02.C150. Явные native-progressive, progressive-remux, waiting, post-download и unsupported результаты**

- **P02.C150.01** — осталось: Различать native-progressive и progressive-remux по фактически выполненному path. [Основание](ProgressiveCompletionChecklist.md).
- **P02.C150.02** — осталось: Различать временное waiting и окончательные unsupported/corrupt/encrypted/missing-decoder. [Основание](ProgressiveCompletionChecklist.md).
- **P02.C150.03** — осталось: Выдавать post-download как ограничение, а не успешную progressive строку. [Основание](ProgressiveCompletionChecklist.md).

**P02.C151. Remux только подтверждённых локальных данных, без перекодирования/ухудшения качества и без изменения оригиналов**

- **P02.C151.01** — осталось: Для каждой новой positive строки подтвердить чтение только опубликованных диапазонов. [Основание](ProgressiveCompletionChecklist.md).
- **P02.C151.02** — осталось: Сравнить compressed payload и исходные PTS/DTS до и после remux. [Основание](ProgressiveCompletionChecklist.md).
- **P02.C151.03** — осталось: Подтвердить отсутствие транскодирования и изменения оригиналов во всей новой матрице. [Основание](ProgressiveCompletionChecklist.md).

**P02.C178. Growing seek: оставшиеся I/O границы (неподтверждённая драйвером отмена disk I/O, AAC projections, original-source/worker path), детерминированные границы другого v2 bridge и расширенная CMainFrame/COM/renderer/AAC/physical-I/O re**

- **P02.C178.01** — осталось: Проверить отмену original-source/worker disk I/O на реальной OS границе. [Основание](ProgressiveCompletionChecklist.md).
- **P02.C178.02** — осталось: Расширить AAC projection и второй v2 bridge на детерминированных seek границах. [Основание](ProgressiveCompletionChecklist.md).
- **P02.C178.03** — осталось: Проверить COM/renderer/CMainFrame reentry при growing seek. [Основание](ProgressiveCompletionChecklist.md).
- **P02.C178.04** — осталось: Проверить supersede/replacement/crash для оставшихся codec paths. [Основание](ProgressiveCompletionChecklist.md).
- **P02.C178.05** — осталось: Определить и проверить расширенные receipt retention/квоты за пределами bounded v1. [Основание](ProgressiveCompletionChecklist.md).


## P04. Аудиокодеки и самостоятельные потоки

**P04.C210. Полная согласованная audio codec matrix: AAC-LC/HE-AAC, Opus, Vorbis, MP3, FLAC, PCM, AC-3/E-AC-3**

- **P04.C210.01** — осталось: HE-AAC: применимые контейнеры, SBR/PS и правильные PCM clocks. [Основание](ProgressiveCompletionChecklist.md).
- **P04.C210.02** — осталось: Opus: оставшиеся one-packet/priming/mapping/chaining layouts за пределами SDK1.106. [Основание](ProgressiveCompletionChecklist.md).
- **P04.C210.03** — осталось: Vorbis: дополнительные setup/rates/layouts/chaining. [Основание](ProgressiveCompletionChecklist.md).
- **P04.C210.04** — осталось: AAC-LC/MP3/FLAC/PCM/AC-3/E-AC-3: завершить согласованную codec/container матрицу. [Основание](ProgressiveCompletionChecklist.md).

**P04.C211. ADTS AAC, MP3, Ogg, FLAC, WAV/PCM; дополнительные rates/layouts и многоканальный звук**

- **P04.C211.01** — осталось: ADTS AAC: оставшиеся rates/layouts. [Основание](ProgressiveCompletionChecklist.md).
- **P04.C211.02** — осталось: MP3: оставшиеся MPEG versions/layers и clocks. [Основание](ProgressiveCompletionChecklist.md).
- **P04.C211.03** — осталось: Ogg Opus/Vorbis: оставшиеся chaining/origin layouts. [Основание](ProgressiveCompletionChecklist.md).
- **P04.C211.04** — осталось: Raw FLAC: дополнительные rates/layouts/block strategies. [Основание](ProgressiveCompletionChecklist.md).
- **P04.C211.05** — осталось: WAV/RF64: float/иные depths/rates/channel masks. [Основание](ProgressiveCompletionChecklist.md).
- **P04.C211.06** — осталось: Многоканальный AAC/Opus/Vorbis/FLAC/PCM/AC-3/E-AC-3 в допустимых контейнерах. [Основание](ProgressiveCompletionChecklist.md).

**P04.C237. Отсутствие аудиоустройства не делает валидный video-only источник ошибочным**

- **P04.C237.01** — осталось: Проверить валидное video-only воспроизведение без аудиоустройства в Linux Qt. [Основание](ProgressiveCompletionChecklist.md).
- **P04.C237.02** — осталось: Проверить тот же сценарий Windows Qt и Classic, включая ошибку backend AO. [Основание](ProgressiveCompletionChecklist.md).

**P04.C238. MPEG-2/MPEG-4 Part 2 для предусмотренных ТЗ загрузок/локальных входов**

- **P04.C238.01** — осталось: MPEG-2 video: progressive reader/remux и decoder admission. [Основание](ProgressiveCompletionChecklist.md).
- **P04.C238.02** — осталось: MPEG-4 Part2: configuration/B-frames и progressive playback. [Основание](ProgressiveCompletionChecklist.md).
- **P04.C238.03** — осталось: Подтвердить соответствующие реальные/локальные входы из ТЗ и отрицательные случаи. [Основание](ProgressiveCompletionChecklist.md).


## P05. Непоследовательная запись и большие ресурсы

**P05.C248. Карта ranges в оставшихся playback-режимах: sparse holes, out-of-order запись и доступные островки данных**

- **P05.C248.01** — осталось: Sparse holes в ещё не покрытых playback modes. [Основание](ProgressiveCompletionChecklist.md).
- **P05.C248.02** — осталось: Out-of-order публикация диапазонов и отдельные доступные островки. [Основание](ProgressiveCompletionChecklist.md).
- **P05.C248.03** — осталось: Seek и готовность по фактической карте диапазонов, а не по сплошному размеру файла. [Основание](ProgressiveCompletionChecklist.md).

**P05.C249. Незаписанные дыры не читаются как нули; file size не заменяет подтверждение writer**

- **P05.C249.01** — осталось: Незаписанные holes не выдаются декодеру как нули во всех допущенных путях. [Основание](ProgressiveCompletionChecklist.md).
- **P05.C249.02** — осталось: Физический размер файла не заменяет опубликованные ranges/integrity. [Основание](ProgressiveCompletionChecklist.md).

**P05.C250. Масштабируемые media indices и внешний индекс digest без огромного командного JSON**

- **P05.C250.01** — осталось: Масштабировать media indices до согласованного большого корпуса. [Основание](ProgressiveCompletionChecklist.md).
- **P05.C250.02** — осталось: Расширить внешний digest index за пределами уже проверенного RF64 среза. [Основание](ProgressiveCompletionChecklist.md).
- **P05.C250.03** — осталось: Передавать большие наборы без огромного JSON и проверить память/дисковый кэш. [Основание](ProgressiveCompletionChecklist.md).

**P05.C251. Быстрый поиск/старт большого каталога без полного предварительного сканирования**

- **P05.C251.01** — осталось: Старт большого каталога сегментов без полного предварительного сканирования. [Основание](ProgressiveCompletionChecklist.md).
- **P05.C251.02** — осталось: Быстрый seek и reopen большого каталога с ограниченным числом handles. [Основание](ProgressiveCompletionChecklist.md).


## P06. Временная шкала и буферизация

**P06.C255. VFR, разные timebase, ненулевые/отрицательные offsets, preroll, edit lists и gapless metadata во всей матрице**

- **P06.C255.01** — осталось: Ненулевые и отрицательные origins/offsets в оставшейся матрице. [Основание](ProgressiveCompletionChecklist.md).
- **P06.C255.02** — осталось: Разные timebase и fractional clocks в ещё не покрытых сочетаниях. [Основание](ProgressiveCompletionChecklist.md).
- **P06.C255.03** — осталось: Preroll/edit lists/gapless за пределами известных AAC/Opus профилей. [Основание](ProgressiveCompletionChecklist.md).
- **P06.C255.04** — осталось: VFR/endpoint semantics и неизвестные длина/конечная точка за пределами опубликованных MP4/WebM режимов. [Основание](ProgressiveCompletionChecklist.md).

**P06.C256. Нужные варианты TS access units, discontinuity и rollover транспортных часов**

- **P06.C256.01** — осталось: TS access units для предусмотренных ещё не покрытых кодеков. [Основание](ProgressiveCompletionChecklist.md).
- **P06.C256.02** — осталось: TS discontinuity без ложного EOF/скачка общей шкалы. [Основание](ProgressiveCompletionChecklist.md).
- **P06.C256.03** — осталось: Rollover транспортных часов и восстановление после seek. [Основание](ProgressiveCompletionChecklist.md).

**P06.C257. Доступная граница по обязательным выбранным потокам; исходные PTS/DTS не обнуляются независимо**

- **P06.C257.01** — осталось: Общая доступная граница обязательных выбранных потоков во всей матрице. [Основание](ProgressiveCompletionChecklist.md).
- **P06.C257.02** — осталось: Сохранение относительных PTS/DTS при разных началах дорожек. [Основание](ProgressiveCompletionChecklist.md).

**P06.C258. Start/resume/buffer policies, ручная пауза и восстановление синхронизации после ожидания/seek**

- **P06.C258.01** — осталось: Проверить start10s/restart5s/low-water1s и короткие законченные источники во всей матрице. [Основание](ProgressiveCompletionChecklist.md).
- **P06.C258.02** — осталось: Сохранить ручную паузу при append/complete во всём корпусе. [Основание](ProgressiveCompletionChecklist.md).
- **P06.C258.03** — осталось: Восстановить A/V sync после ожидания/seek без постоянного offset workaround. [Основание](ProgressiveCompletionChecklist.md).


## P07. Конечные HLS/DASH

**P07.C262. Локальные init/media segments, параллельное получение, gaps, discontinuity и rebasing**

- **P07.C262.01** — осталось: Локальные init/media HLS/DASH segments вне текущего узкого admission. [Основание](ProgressiveCompletionChecklist.md).
- **P07.C262.02** — осталось: Параллельное получение сегментов и gaps. [Основание](ProgressiveCompletionChecklist.md).
- **P07.C262.03** — осталось: Discontinuity/rebasing общей сегментной шкалы. [Основание](ProgressiveCompletionChecklist.md).

**P07.C263. Расширенная сегментная codec/container matrix, включая Vorbis где применимо**

- **P07.C263.01** — осталось: Зафиксировать конечную расширенную segmented codec/container матрицу. [Основание](ProgressiveCompletionChecklist.md).
- **P07.C263.02** — осталось: Подключить и проверить недостающие video/audio пары. [Основание](ProgressiveCompletionChecklist.md).
- **P07.C263.03** — осталось: Vorbis в допустимых сегментных входах. [Основание](ProgressiveCompletionChecklist.md).

**P07.C264. Состояние аудио между сегментами, смена init/config и безопасный доступ к ключевым кадрам**

- **P07.C264.01** — осталось: Сохранить audio decoder state между сегментами. [Основание](ProgressiveCompletionChecklist.md).
- **P07.C264.02** — осталось: Обработать смену init/config на безопасной decode point. [Основание](ProgressiveCompletionChecklist.md).
- **P07.C264.03** — осталось: Не открывать ещё недоступный keyframe/инициализацию. [Основание](ProgressiveCompletionChecklist.md).

**P07.C265. Длинные каталоги, ограниченные handles/cache и повторное открытие ресурсов**

- **P07.C265.01** — осталось: Длинные сегментные каталоги и Unicode. [Основание](ProgressiveCompletionChecklist.md).
- **P07.C265.02** — осталось: Bounded handles/cache при большом числе ресурсов. [Основание](ProgressiveCompletionChecklist.md).
- **P07.C265.03** — осталось: Retire/reopen ресурсов без потери identity. [Основание](ProgressiveCompletionChecklist.md).


## P08. Смена качества и конфигурации

**P08.C269. Контролируемая переинициализация decoder на подходящей точке декодирования**

- **P08.C269.01** — осталось: Найти допустимую decode point новой конфигурации. [Основание](ProgressiveCompletionChecklist.md).
- **P08.C269.02** — осталось: Переинициализировать decoder и проверить old/new isolation. [Основание](ProgressiveCompletionChecklist.md).

**P08.C270. Сохранение позиции, pause, speed, volume и выбранных дорожек**

- **P08.C270.01** — осталось: Сохранить позицию и ручную паузу при смене качества. [Основание](ProgressiveCompletionChecklist.md).
- **P08.C270.02** — осталось: Сохранить speed/volume и выбранные audio/subtitle tracks. [Основание](ProgressiveCompletionChecklist.md).

**P08.C271. Недостаточные данные новой конфигурации дают ожидание с точной причиной**

- **P08.C271.01** — осталось: Ожидать недостающие initialization/configuration bytes. [Основание](ProgressiveCompletionChecklist.md).
- **P08.C271.02** — осталось: Выдать точную причину и продолжить после публикации без второго скачивания. [Основание](ProgressiveCompletionChecklist.md).


## P09. Динамические аудиодорожки

**P09.C275. Несколько audio в muxed и segmented источниках; динамическое добавление/отключение**

- **P09.C275.01** — осталось: Несколько audio в muxed источнике за пределами текущего admission. [Основание](ProgressiveCompletionChecklist.md).
- **P09.C275.02** — осталось: Несколько audio в segmented источнике. [Основание](ProgressiveCompletionChecklist.md).
- **P09.C275.03** — осталось: Позднее добавление и удаление audio без перезапуска загрузки. [Основание](ProgressiveCompletionChecklist.md).

**P09.C276. Стабильные ID, language/label/default/forced и сохранение выбора**

- **P09.C276.01** — осталось: Стабильные track IDs при revisions/reconnect. [Основание](ProgressiveCompletionChecklist.md).
- **P09.C276.02** — осталось: Сохранять language/label/default/forced и пользовательский выбор. [Основание](ProgressiveCompletionChecklist.md).

**P09.C277. Невыбранные optional tracks не тормозят просмотр; выбранная недокачанная дорожка ждёт свои данные**

- **P09.C277.01** — осталось: Невыбранные optional audio не ограничивают primary. [Основание](ProgressiveCompletionChecklist.md).
- **P09.C277.02** — осталось: Выбранная partial audio ждёт только необходимые диапазоны без молчаливой замены языка. [Основание](ProgressiveCompletionChecklist.md).

**P09.C278. Ошибка optional audio изолируется и объясняется; язык не подменяется молча**

- **P09.C278.01** — осталось: Изоляция optional audio errors на новых paths. [Основание](ProgressiveCompletionChecklist.md).
- **P09.C278.02** — осталось: Показывать причину и дать явный выбор восстановления. [Основание](ProgressiveCompletionChecklist.md).


## P10. Субтитры и вложения

**P10.C282. Растущие SRT/WebVTT/ASS/SSA/TTML, адаптеры JSON3/SRV**

- **P10.C282.01** — осталось: Растущий SRT за пределами complete-only текущего профиля. [Основание](ProgressiveCompletionChecklist.md).
- **P10.C282.02** — осталось: Растущий WebVTT. [Основание](ProgressiveCompletionChecklist.md).
- **P10.C282.03** — осталось: Растущий ASS/SSA. [Основание](ProgressiveCompletionChecklist.md).
- **P10.C282.04** — осталось: Растущий TTML. [Основание](ProgressiveCompletionChecklist.md).
- **P10.C282.05** — осталось: Документированные JSON3/SRV адаптеры. [Основание](ProgressiveCompletionChecklist.md).

**P10.C283. UTF-8/UTF-16/BOM и неполные cues; только завершённые события без дубликатов**

- **P10.C283.01** — осталось: UTF-8/UTF-16/BOM на границах обновления bytes. [Основание](ProgressiveCompletionChecklist.md).
- **P10.C283.02** — осталось: Отбрасывать неполный cue до подтверждения. [Основание](ProgressiveCompletionChecklist.md).
- **P10.C283.03** — осталось: Повторные revisions не дублируют subtitle events. [Основание](ProgressiveCompletionChecklist.md).

**P10.C284. Embedded mov_text/WebVTT/ASS/SubRip; PGS/DVD subtitles с нужными headers/palettes в локальных контейнерах**

- **P10.C284.01** — осталось: Embedded mov_text и WebVTT. [Основание](ProgressiveCompletionChecklist.md).
- **P10.C284.02** — осталось: Embedded ASS и SubRip. [Основание](ProgressiveCompletionChecklist.md).
- **P10.C284.03** — осталось: Локальные PGS с обязательными headers/palettes. [Основание](ProgressiveCompletionChecklist.md).
- **P10.C284.04** — осталось: Локальные DVD subtitle с обязательными headers/palettes. [Основание](ProgressiveCompletionChecklist.md).

**P10.C285. Styles/settings/language/default/forced; ограничения конвертации описаны без ложного обещания полного оформления**

- **P10.C285.01** — осталось: ASS styles и WebVTT settings. [Основание](ProgressiveCompletionChecklist.md).
- **P10.C285.02** — осталось: Поддержанная часть TTML styling с явными ограничениями. [Основание](ProgressiveCompletionChecklist.md).
- **P10.C285.03** — осталось: Language/default/forced и выбор subtitle track без потери при update. [Основание](ProgressiveCompletionChecklist.md).

**P10.C286. Безопасные bounded fonts/attachments без установки в систему и внешних XML/HTML ресурсов**

- **P10.C286.01** — осталось: Bounded font/attachment parser без установки в систему. [Основание](ProgressiveCompletionChecklist.md).
- **P10.C286.02** — осталось: Запрет внешних XML entities/HTML resources. [Основание](ProgressiveCompletionChecklist.md).
- **P10.C286.03** — осталось: Негативные size/path/malformed проверки attachments. [Основание](ProgressiveCompletionChecklist.md).


## P11. Главы и метаданные

**P11.C290. Совместная обработка container/writer chapters, стабильные IDs, late updates без сброса позиции**

- **P11.C290.01** — осталось: Согласовать container и writer главы по стабильным IDs. [Основание](ProgressiveCompletionChecklist.md).
- **P11.C290.02** — осталось: Late chapter updates без сброса позиции и дубликатов. [Основание](ProgressiveCompletionChecklist.md).

**P11.C291. Неизвестная финальная duration и главы за доступной границей**

- **P11.C291.01** — осталось: Работа глав при неизвестной конечной duration. [Основание](ProgressiveCompletionChecklist.md).
- **P11.C291.02** — осталось: Переход к ещё недоступной главе через общие availability rules. [Основание](ProgressiveCompletionChecklist.md).

**P11.C292. Covers/preview, названия дорожек, rotation/aspect ratio и HDR/color metadata**

- **P11.C292.01** — осталось: Covers/preview не блокируют required A/V. [Основание](ProgressiveCompletionChecklist.md).
- **P11.C292.02** — осталось: Названия дорожек и сохранение metadata при remux/finalize. [Основание](ProgressiveCompletionChecklist.md).
- **P11.C292.03** — осталось: Rotation/aspect ratio из контейнера/writer. [Основание](ProgressiveCompletionChecklist.md).
- **P11.C292.04** — осталось: Сохранить и диагностировать HDR/color metadata; physical output отдельно. [Основание](ProgressiveCompletionChecklist.md).


## P12. Live/event HLS и dynamic DASH

**P12.C296. Live/event HLS и dynamic DASH через локальную публикацию writer**

- **P12.C296.01** — осталось: Live/event HLS через локальный writer. [Основание](ProgressiveCompletionChecklist.md).
- **P12.C296.02** — осталось: Dynamic DASH через локальный writer. [Основание](ProgressiveCompletionChecklist.md).

**P12.C297. Неизвестная duration, движущееся окно, отдельная целевая задержка и reconnect**

- **P12.C297.01** — осталось: Неизвестная duration и движущееся live окно. [Основание](ProgressiveCompletionChecklist.md).
- **P12.C297.02** — осталось: Целевая задержка, reconnect и восстановление reader state. [Основание](ProgressiveCompletionChecklist.md).

**P12.C298. Отсутствие сегмента/heartbeat не означает EOF; конец подтверждает writer**

- **P12.C298.01** — осталось: Пропавший сегмент/heartbeat переводит в waiting, не EOF. [Основание](ProgressiveCompletionChecklist.md).
- **P12.C298.02** — осталось: Только явный writer end завершает live. [Основание](ProgressiveCompletionChecklist.md).

**P12.C299. Retirement вышедших ресурсов только после освобождения всеми readers; seek за окном имеет отдельную причину**

- **P12.C299.01** — осталось: Retirement вышедших ресурсов после release всех readers. [Основание](ProgressiveCompletionChecklist.md).
- **P12.C299.02** — осталось: Явный reason для seek вне live окна. [Основание](ProgressiveCompletionChecklist.md).


## P13. Жизненный цикл, I/O и безопасность

**P13.C303. Оставшиеся UI-blocking пути probe/index/remux/read/teardown, Qt GL cleanup и обычные Classic graphs**

- **P13.C303.01** — осталось: Оставшиеся blocking probe/index/remux/read paths вне UI. [Основание](ProgressiveCompletionChecklist.md).
- **P13.C303.02** — осталось: Qt OpenGL cleanup без зависания UI. [Основание](ProgressiveCompletionChecklist.md).
- **P13.C303.03** — осталось: Обычные UI-owned Classic graph/COM teardown. [Основание](ProgressiveCompletionChecklist.md).

**P13.C304. Управляемая отмена ожиданий, bounded остановка внешнего процесса, фактические release/filesClosed receipts**

- **P13.C304.01** — осталось: Отмена управляемых ожиданий при Stop/Close/Cancel. [Основание](ProgressiveCompletionChecklist.md).
- **P13.C304.02** — осталось: Bounded остановка зависшего внешнего worker. [Основание](ProgressiveCompletionChecklist.md).
- **P13.C304.03** — осталось: FilesClosed/release receipts отражают реальное закрытие, включая registry contention. [Основание](ProgressiveCompletionChecklist.md).
- **P13.C304.04** — осталось: Проверить deadline ≤2s на всём поддержанном корпусе; device-stalled I/O отделён явно. [Основание](ProgressiveCompletionChecklist.md).

**P13.C308. Writer/player crash, resume по идентичности bytes, несколько readers, rename и переход на итоговый файл**

- **P13.C308.01** — осталось: Resume после writer/player/worker crash по identity bytes. [Основание](ProgressiveCompletionChecklist.md).
- **P13.C308.02** — осталось: Изоляция нескольких readers при crash/Stop/reopen. [Основание](ProgressiveCompletionChecklist.md).
- **P13.C308.03** — осталось: Rename/finalize для всех допущенных контейнеров. [Основание](ProgressiveCompletionChecklist.md).
- **P13.C308.04** — осталось: Переход к итоговому файлу с сохранением позиции/паузы. [Основание](ProgressiveCompletionChecklist.md).

**P13.C309. Ошибка preview не отменяет download; merge/writer errors не становятся успешным EOF**

- **P13.C309.01** — осталось: Preview error не отменяет загрузку и не меняет архив. [Основание](ProgressiveCompletionChecklist.md).
- **P13.C309.02** — осталось: Writer/merge failure не становится successful EOF. [Основание](ProgressiveCompletionChecklist.md).

**P13.C319. Внешний Python launcher с длинным собственным TEMP остаётся OPEN: пять owned controls/diagnostics и три stack snapshots локализуют ожидание GetTempPathW внутри Windows CreateProcess до возврата Popen**

- **P13.C319.02** — внешнее условие: Проверить внешний Python CreateProcess с собственным long parent TEMP без изменения Harvester/Python/Windows. [Основание](ProgressiveCompletionChecklist.md).
  Внешний проект/условие вне разрешённой работы FoxicMP; не менять Harvester, Python или Windows.

**P13.C321. Предельные длины компонентов/пути, Unicode private CLI argv и полный long-path rename matrix. Natural EOF/error на длинных roots — отдельная расширенная матрица, не результат short-root CLI-среза**

- **P13.C321.01** — осталось: Предельная длина компонентов и полного пути. [Основание](ProgressiveCompletionChecklist.md).
- **P13.C321.02** — осталось: Unicode private CLI argv на Windows и Linux. [Основание](ProgressiveCompletionChecklist.md).
- **P13.C321.03** — осталось: Полная long-path rename/substitution матрица. [Основание](ProgressiveCompletionChecklist.md).
- **P13.C321.04** — осталось: Natural EOF/error на длинных source roots за пределами короткого CLI контроля. [Основание](ProgressiveCompletionChecklist.md).

**P13.C322. Owner/permissions, reparse/symlink/path traversal/substitution, IPC current-user, секреты не попадают в логи**

- **P13.C322.01** — осталось: Owner/permissions/current-user IPC для всех новых путей. [Основание](ProgressiveCompletionChecklist.md).
- **P13.C322.02** — осталось: Reparse/symlink/traversal/substitution для всех новых путей. [Основание](ProgressiveCompletionChecklist.md).
- **P13.C322.03** — осталось: Проверить отсутствие cookies/tokens/signed URLs в обычных и diagnostic logs. [Основание](ProgressiveCompletionChecklist.md).

**P13.C323. Network/removable filesystem capabilities только по отдельным доказательствам; обязательная база — local fixed drive**

- **P13.C323.01** — отложено: Network filesystem: отдельные identity/read/write/disconnect доказательства. [Основание](ProgressiveCompletionChecklist.md).
  Пользователь отложил перенос/аппаратную проверку; network/removable допуски отдельно условны и не объявлены поддержанными.
- **P13.C323.02** — отложено: Removable filesystem: отдельные identity/read/write/disconnect доказательства. [Основание](ProgressiveCompletionChecklist.md).
  Пользователь отложил перенос/аппаратную проверку; network/removable допуски отдельно условны и не объявлены поддержанными.


## P14. API, интерфейс и диагностика

**P14.C327. Общие точные reasonCodes и states для всех новых режимов; ok принятия команды не означает playing**

- **P14.C327.01** — осталось: Общие точные states/reasonCodes во всех новых режимах. [Основание](ProgressiveCompletionChecklist.md).
- **P14.C327.02** — осталось: Разделять command accepted, preparation, waiting, playing и terminal outcome. [Основание](ProgressiveCompletionChecklist.md).

**P14.C328. Название/источник/выбранные дорожки/доступная граница/буфер/причина ожидания**

- **P14.C328.01** — осталось: Название/источник/выбранные дорожки во всех новых modes. [Основание](ProgressiveCompletionChecklist.md).
- **P14.C328.02** — осталось: Показывать доступную границу/буфер/конкретную причину waiting. [Основание](ProgressiveCompletionChecklist.md).

**P14.C329. Pause/Stop/Seek, главы и track menus остаются отзывчивыми; activate=false не перехватывает фокус**

- **P14.C329.01** — осталось: Отзывчивые Pause/Stop/Seek/track menus/главы во всей матрице. [Основание](ProgressiveCompletionChecklist.md).
- **P14.C329.02** — осталось: Проверить activate=false и отсутствие захвата фокуса. [Основание](ProgressiveCompletionChecklist.md).

**P14.C330. Capabilities отражают фактические EXE/decoder builds; v1/client/legacy adapters не регрессировали**

- **P14.C330.01** — осталось: Сверить фактические shipped EXE/worker/decoder capabilities. [Основание](ProgressiveCompletionChecklist.md).
- **P14.C330.02** — осталось: Проверить v1/client/legacy migration corpus на итоговых сборках. [Основание](ProgressiveCompletionChecklist.md).

**P14.RANGE. Запрос нужного диапазона/приоритета writer и порционная передача больших metadata**

- **P14.RANGE.01** — осталось: Согласовать capability запроса диапазона/приоритета и точный player/client ответ. [Основание](ProgressiveTechnicalSpec-2026-10-25.ru.md).
- **P14.RANGE.02** — осталось: Передать запрос через SDK-клиент; writer без этой capability оставляет просмотр в честном waiting. [Основание](ProgressiveTechnicalSpec-2026-10-25.ru.md).
- **P14.RANGE.03** — осталось: Передавать большие главы/субтитры ограниченными порциями или локальными ресурсами без увеличения JSON лимита. [Основание](ProgressiveTechnicalSpec-2026-10-25.ru.md).


## P15. Приёмка, ресурсы и задержки

**P15.C146. Полные waiting/replay/shutdown/crash/stress проверки публичного continuous-remux SDK трёх редакций. Далее — Linux release latency, зависший OS I/O/cancellation и расширенная race/stress matrix**

- **P15.C146.01** — осталось: Свести waiting/replay/replacement/re-arm/failure сценарии всего поддержанного continuous корпуса. [Основание](ProgressiveCompletionChecklist.md).
- **P15.C146.02** — осталось: Проверить worker/player crash и shutdown всего поддержанного корпуса. [Основание](ProgressiveCompletionChecklist.md).
- **P15.C146.03** — осталось: Проверить длительный и циклический stress всего корпуса без замены немедленных budget поздним idle. [Основание](ProgressiveCompletionChecklist.md).
- **P15.C146.04** — осталось: Повторить release latency и реальные OS-I/O cancellation границы отдельно от успешных file/null тестов. [Основание](ProgressiveCompletionChecklist.md).

**P15.C334. Общая positive/negative matrix трёх редакций, минимум 10 секунд просмотра до complete**

- **P15.C334.01** — осталось: Зафиксировать каждую обязательную positive/negative строку и её expected result. [Основание](ProgressiveCompletionChecklist.md).
- **P15.C334.02** — осталось: Positive playback ≥10s до complete во всех трёх редакциях. [Основание](ProgressiveCompletionChecklist.md).
- **P15.C334.03** — осталось: Проверить неполученные bytes/ложный EOF/исходники/metadata на всём корпусе. [Основание](ProgressiveCompletionChecklist.md).
- **P15.C334.04** — осталось: У каждой negative/SKIP строки точная причина и подтверждённая ОС; запрет collector-only PASS. [Основание](ProgressiveCompletionChecklist.md).

**P15.C335. Неравномерная запись, writer pauses 10/30/60 s, partial structures, holes, ошибки, disk full, cancel/crash**

- **P15.C335.01** — осталось: Неравномерные скорости обязательных потоков. [Основание](ProgressiveCompletionChecklist.md).
- **P15.C335.02** — осталось: Writer pauses 10/30/60s с корректным heartbeat. [Основание](ProgressiveCompletionChecklist.md).
- **P15.C335.03** — осталось: Partial headers/packets, holes и out-of-order запись. [Основание](ProgressiveCompletionChecklist.md).
- **P15.C335.04** — осталось: Disk full, cancel и crash на границах ввода/вывода. [Основание](ProgressiveCompletionChecklist.md).

**P15.C336. Реальные разрешённые YT/VK/Rutube direct/HLS/DASH сценарии с protocol/format IDs и offline fixtures без секретных URL**

- **P15.C336.01** — осталось: YouTube direct/HLS/DASH там, где фактически доступно. [Основание](ProgressiveCompletionChecklist.md).
- **P15.C336.02** — осталось: VK direct/HLS/DASH там, где фактически доступно. [Основание](ProgressiveCompletionChecklist.md).
- **P15.C336.03** — осталось: Rutube direct/HLS/DASH там, где фактически доступно. [Основание](ProgressiveCompletionChecklist.md).
- **P15.C336.04** — осталось: Сохранить protocol/format IDs и воспроизводимые offline fixtures без секретных URL. [Основание](ProgressiveCompletionChecklist.md).

**P15.C344. Длительные загрузки, минимум 100 start/cancel циклов, несколько readers, RAM/cache/temp/CPU/handles/threads без нарастающих утечек**

- **P15.C344.01** — осталось: Длительные runs на расширенном codec/container корпусе. [Основание](ProgressiveCompletionChecklist.md).
- **P15.C344.02** — осталось: Минимум100 start/cancel циклов остальных modes. [Основание](ProgressiveCompletionChecklist.md).
- **P15.C344.03** — осталось: Несколько readers и смешанные редакции. [Основание](ProgressiveCompletionChecklist.md).
- **P15.C344.04** — осталось: Проверить RAM/cache/temp/CPU/handles/threads budgets без нарастающих утечек. [Основание](ProgressiveCompletionChecklist.md).

**P15.C345. Обычные завершённые файлы, пользовательские профили и shell registration не регрессировали**

- **P15.C345.01** — осталось: Регрессия обычного completed-file corpus. [Основание](ProgressiveCompletionChecklist.md).
- **P15.C345.02** — осталось: Сохранность пользовательских профилей и соседних установок. [Основание](ProgressiveCompletionChecklist.md).
- **P15.C345.03** — осталось: Shell registration/HKCU до/после и независимость IPC/EXE/INI. [Основание](ProgressiveCompletionChecklist.md).

**P15.C368. Старт после готового буфера ≤3 s, resume ≤2 s на согласованном эталонном ПК; remux preparation измеряется отдельно**

- **P15.C368.01** — осталось: Согласовать эталонный ПК и измерительную методику start/resume. [Основание](ProgressiveCompletionChecklist.md).
- **P15.C368.02** — осталось: Измерить ready-buffer start ≤3s и resume ≤2s во всей матрице. [Основание](ProgressiveCompletionChecklist.md).
- **P15.C368.03** — осталось: Измерить remux preparation отдельно от ready-start. [Основание](ProgressiveCompletionChecklist.md).

**P15.C369. Абсолютная A/V ошибка ≤80 ms; накопленный drift за 30 min ≤40 ms, с описанной методикой физического выхода**

- **P15.C369.01** — отложено: Согласовать физический flash/beep стенд и latency устройства. [Основание](ProgressiveCompletionChecklist.md).
  Пользователь отложил перенос/аппаратную проверку; network/removable допуски отдельно условны и не объявлены поддержанными.
- **P15.C369.02** — отложено: Измерить абсолютную A/V ошибку ≤80ms. [Основание](ProgressiveCompletionChecklist.md).
  Пользователь отложил перенос/аппаратную проверку; network/removable допуски отдельно условны и не объявлены поддержанными.
- **P15.C369.03** — отложено: Измерить накопленный drift за30min ≤40ms. [Основание](ProgressiveCompletionChecklist.md).
  Пользователь отложил перенос/аппаратную проверку; network/removable допуски отдельно условны и не объявлены поддержанными.


## P16. Внешняя интеграция Harvester

**P16.C349. Отдельное разрешение/задача на подключение реальных download events и кнопки preview в Harvester**

- **P16.C349.01** — внешнее условие: Получить отдельную задачу/разрешение на реальные Harvester events/button. [Основание](ProgressiveCompletionChecklist.md).
  Внешний проект/условие вне разрешённой работы FoxicMP; не менять Harvester, Python или Windows.

**P16.C350. Сквозные start/update/complete/error/cancel/release из реального Harvester во всех трёх редакциях**

- **P16.C350.01** — внешнее условие: Реальный Harvester → Linux Qt: start/update/complete/error/cancel/release. [Основание](ProgressiveCompletionChecklist.md).
  Внешний проект/условие вне разрешённой работы FoxicMP; не менять Harvester, Python или Windows.
- **P16.C350.02** — внешнее условие: Реальный Harvester → Windows Qt: тот же conformance. [Основание](ProgressiveCompletionChecklist.md).
  Внешний проект/условие вне разрешённой работы FoxicMP; не менять Harvester, Python или Windows.
- **P16.C350.03** — внешнее условие: Реальный Harvester → Classic: тот же conformance. [Основание](ProgressiveCompletionChecklist.md).
  Внешний проект/условие вне разрешённой работы FoxicMP; не менять Harvester, Python или Windows.


## P17. Итоговые пакеты и GitHub release

**P17.C361. Три проверенные сборки, Windows EXE/MSI и Linux .deb, SDK и инструкции интеграции**

- **P17.C361.01** — осталось: Три окончательные проверенные сборки из одного commit. [Основание](ProgressiveCompletionChecklist.md).
- **P17.C361.02** — осталось: Windows Classic EXE и MSI. [Основание](ProgressiveCompletionChecklist.md).
- **P17.C361.03** — осталось: Windows Qt EXE и MSI. [Основание](ProgressiveCompletionChecklist.md).
- **P17.C361.04** — осталось: Linux DEB и portable. [Основание](ProgressiveCompletionChecklist.md).
- **P17.C361.05** — осталось: Совместимый SDK, writer/client и инструкции интеграции. [Основание](ProgressiveCompletionChecklist.md).

**P17.C362. Исходники, воспроизводимые fixtures, версии/build options/licenses компонентов и итоговая matrix с exit codes**

- **P17.C362.01** — осталось: Исходники и воспроизводимые test fixtures с происхождением. [Основание](ProgressiveCompletionChecklist.md).
- **P17.C362.02** — осталось: Backend versions/build options/licenses. [Основание](ProgressiveCompletionChecklist.md).
- **P17.C362.03** — осталось: Итоговая matrix с точными native exit codes. [Основание](ProgressiveCompletionChecklist.md).

**P17.C363. Независимые установки/профили/регистрация; Linux обновлён с backup и проверкой сохранности INI**

- **P17.C363.01** — осталось: Clean install/upgrade Windows Classic и Qt без смешения профилей. [Основание](ProgressiveCompletionChecklist.md).
- **P17.C363.02** — осталось: Clean install/upgrade Linux с backup и сохранением INI. [Основание](ProgressiveCompletionChecklist.md).
- **P17.C363.03** — осталось: Проверка независимости регистраций/IPC и соседних MPC/MPMY установок. [Основание](ProgressiveCompletionChecklist.md).

**P17.C364. Итоговый GitHub release и проверка скачанных assets/checksums/source commit**

- **P17.C364.01** — осталось: Опубликовать окончательный GitHub release после итоговых gates. [Основание](ProgressiveCompletionChecklist.md).
- **P17.C364.02** — осталось: Скачать опубликованные assets и проверить checksums/source commit. [Основание](ProgressiveCompletionChecklist.md).


## H. Атрибуция четырёх исторических сбоев

**H.C119. Локализовать единичное превышение 2 s автоматического release source/cache на паузе в исправленной Linux-сборке**

- **H.C119.01** — внешнее условие: Сохранить close-stage/PID/TID/lock/native-I/O данные при новом естественном Linux release >2s. [Основание](ProgressiveCompletionChecklist.md).
  Нужен новый естественный отказ с сохранёнными исходными данными; бессрочные повторы успешного случая не назначаются.
- **H.C119.02** — внешнее условие: Установить причину по захваченному естественному отказу; не приписывать ему искусственный registry lock. [Основание](ProgressiveCompletionChecklist.md).
  Нужен новый естественный отказ с сохранёнными исходными данными; бессрочные повторы успешного случая не назначаются.

**H.C129. Ограниченный continuous lifecycle stress и paused expiry: итоговый повтор по 100 измеряемых +4 warmup cycles и 12 настоящих 30000 ms timeout каждой редакции прошёл прежние budgets**

- **H.C129.01** — внешнее условие: Сохранить baseline и failed resource snapshot до assert при новом Classic heartbeat-12 отказе. [Основание](ProgressiveCompletionChecklist.md).
  Нужен новый естественный отказ с сохранёнными исходными данными; бессрочные повторы успешного случая не назначаются.
- **H.C129.02** — внешнее условие: При воспроизведении установить инициатора; не считать DWM-fix доказательством причины старого события. [Основание](ProgressiveCompletionChecklist.md).
  Нужен новый естественный отказ с сохранёнными исходными данными; бессрочные повторы успешного случая не назначаются.

**H.C137. Исторический Windows Qt `windowClose:reader-release` assertion FAIL остаётся неатрибутированным: нет исходного JSON и индивидуального времени ожидания**

- **H.C137.01** — внешнее условие: Сохранять первый JSON/receipt и точное время неуспешного Windows Qt наблюдения. [Основание](ProgressiveCompletionChecklist.md).
  Нужен новый естественный отказ с сохранёнными исходными данными; бессрочные повторы успешного случая не назначаются.
- **H.C137.02** — внешнее условие: При воспроизведении атрибутировать старый reader-release assertion отдельно от времени закрытия окна. [Основание](ProgressiveCompletionChecklist.md).
  Нужен новый естественный отказ с сохранёнными исходными данными; бессрочные повторы успешного случая не назначаются.

**H.C158. AAC heartbeat-срез 1.84: Linux/Windows Qt — по 17 PASS и точный exit 0**

- **H.C158.01** — внешнее условие: Захватить origin/callback временных TppWorkerThread при превышении строгого AAC budget. [Основание](ProgressiveCompletionChecklist.md).
  Нужен новый естественный отказ с сохранёнными исходными данными; бессрочные повторы успешного случая не назначаются.
- **H.C158.02** — внешнее условие: Установить инициирующий callback и проверить исходный немедленный resource gate. [Основание](ProgressiveCompletionChecklist.md).
  Нужен новый естественный отказ с сохранёнными исходными данными; бессрочные повторы успешного случая не назначаются.


## F2. Обе Qt-платформы и автоматическая проверка

- **F2.03** — осталось: Классифицировать весь Qt-корпус: общий/OS-adapter/Linux-only/Windows-only. [Основание](FoxicMPFamilyPlan.md).
- **F2.04** — осталось: Подключить самостоятельный Windows Qt CI workflow. [Основание](FoxicMPFamilyPlan.md).
- **F2.05** — осталось: Проверять portable Windows Qt на системном PATH без developer SDK. [Основание](FoxicMPFamilyPlan.md).
- **F2.06** — осталось: Убрать Linux-only backend варианты из Windows Qt и показывать реальный fallback. [Основание](FoxicMPFamilyPlan.md).

## F5. Мастер переноса библиотеки и предпочтений

- **F5.01** — отложено: Нейтральный формат истории/избранного и общих предпочтений Classic↔Qt. [Основание](FoxicMPFamilyPlan.md).
  Пользователь отложил перенос/аппаратную проверку; network/removable допуски отдельно условны и не объявлены поддержанными.
- **F5.02** — отложено: Предпросмотр импорта с несовместимыми полями и Windows/Linux paths. [Основание](FoxicMPFamilyPlan.md).
  Пользователь отложил перенос/аппаратную проверку; network/removable допуски отдельно условны и не объявлены поддержанными.
- **F5.03** — отложено: Проверить перенос без изменения исходной установки/файлов. [Основание](FoxicMPFamilyPlan.md).
  Пользователь отложил перенос/аппаратную проверку; network/removable допуски отдельно условны и не объявлены поддержанными.

## F6. Мультимедиа и платформенные ограничения

- **F6.03** — осталось: Общие сценарии SRT/ASS, оформления и сохранения subtitle editor. [Основание](FoxicMPFamilyPlan.md).
- **F6.04** — осталось: Сравнить семантику audio delay/gain/mixer/filter диапазонов трёх редакций. [Основание](FoxicMPFamilyPlan.md).
- **F6.05** — осталось: Общие пресеты crop/pan/цвет/скейлеры и диагностика фактического decoder/renderer/AO. [Основание](FoxicMPFamilyPlan.md).
- **F6.06** — осталось: WinLIRC/SMTC/global hotkeys: определить применимую платформенную совместимость без требования одинакового backend. [Основание](FoxicMPFamilyPlan.md).
- **F6.07** — осталось: Расширенный безопасный GLSL hook importer за пределами текущего ограниченного профиля. [Основание](FoxicMPFamilyPlan.md).
- **F6.08** — отложено: Физические мониторы/GPU/HDR/стерео/HDMI/S-PDIF/ресиверы/пульты. [Основание](FoxicMPFamilyPlan.md).
  Пользователь отложил перенос/аппаратную проверку; network/removable допуски отдельно условны и не объявлены поддержанными.
- **F6.09** — отложено: Меню дисков. [Основание](FoxicMPFamilyPlan.md).
  Пользователь отложил перенос/аппаратную проверку; network/removable допуски отдельно условны и не объявлены поддержанными.
- **F6.10** — отложено: Аналоговое ТВ. [Основание](FoxicMPFamilyPlan.md).
  Пользователь отложил перенос/аппаратную проверку; network/removable допуски отдельно условны и не объявлены поддержанными.
- **F6.11** — отложено: Физические устройства захвата/ТВ. [Основание](FoxicMPFamilyPlan.md).
  Пользователь отложил перенос/аппаратную проверку; network/removable допуски отдельно условны и не объявлены поддержанными.

## F7. Общий цикл сборки и версии семейства

- **F7.03** — осталось: Общий CI цикл трёх редакций с aggregate report. [Основание](FoxicMPFamilyPlan.md).
- **F7.04** — осталось: Один процесс сборки пакетов из одного commit и общий release gate. [Основание](FoxicMPFamilyPlan.md).

