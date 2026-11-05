# B-frames: managed source, кэш и настоящие декодеры

5 ноября 2026. Приложение 0.2.0, SDK 1.82. Продолжение
[worker-этапа](ProgressiveContinuousReorder.md).
[Первичные результаты, SHA и exit codes](ProgressiveManagedReorderValidation-2026-11-05.json).
Это внутренний профиль; публичный continuous SDK всё ещё не принимает B-frames.

## Изменение

`StreamRemuxSession` и `CachedRemuxSession` получают явный внутренний
`StreamRemuxProfile::H264ReorderedVideo`. По умолчанию остаётся `H264Video`.
Выбранный профиль определяет запрос к worker и обязательный mode его результата;
manifest или ответ child не могут самостоятельно расширить профиль.
Неизвестное значение enum отвергается до создания ResourceSource/worker.
Лимиты, SHA/ranges, deadlines и reader lifecycle сохранены.

`FragmentIndex`, `StreamOutputStore`, `StreamDecoderCursor`, `ContinuousStreams`
и `CHarvesterSession::OpenContinuous` уже умеют необходимую edit list и
раздельные PTS/DTS: их production code не менялся. Это подтверждено новыми
тестами с реальными декодерами, а не только успешным разбором заголовка.

Public controller, CLI/API, capability и SDK не выбирают новый профиль.
Нет нового main EXE или пользовательского релиза. Установленные плееры,
worker и настройки сохранены; Linux остаётся проверенным `ebb0a806…`.

## Managed pipeline

По четыре 24-секундных профиля каждой сборки: H.264 25 fps с `-bf 2`,
30000/1001 с `-bf 3`, 60000/1001 с `-bf 16` и VFR с `-bf 2`.
Всего на сборку 3239 кадров, после exact seek к 10 s ещё 1889 кадров.
`-bf` — настройка encoder; тест дополнительно проверяет реальные отрицательные
DTS и reorder. Bitstream между ОС не обязан совпадать.

Для каждого профиля:

1. Completed original декодируется libmpv в эталонную PNG-последовательность.
2. Настоящий `input.part` физически содержит только четыре байта. Один
   ResourceSource→worker→cache context ожидает подтверждённые данные.
3. Append-only запись разрезает packets на 4/8/12/20 s. При первом append
   физические bytes на 300 ms опережают публикацию; maximumReadEnd остаётся
   внутри прежнего диапазона. SHA каждого префикса проверяется.
4. Настоящий libmpv через production `ContinuousStreams` читает verified cache;
   Classic sample view независимо читает тот же кэш. Manual pause сохраняется
   при append; закрытие/повторное открытие sample view не останавливает libmpv.
5. Все bytes без writer complete не дают EOF. После complete получены обычный
   decoder EOF, source filesClosed и точное совпадение всех packets и PNG.
6. Новый paused decoder выдаёт один ожидаемый preview первого кадра. После
   exact seek к 10 s **вся последующая** PNG-последовательность равна нужному
   суффиксу эталона; preview проверяется отдельно и не считается seek-кадром.
7. Raw cursor и sample view независимо удерживают физический кэш. Он удаляется
   после последнего close; проверены реальные filesClosed receipts, forget,
   нулевой cache budget и SHA исходника.

Packet payload и рациональные PTS/DTS/duration полного output точно совпадают
с ffprobe-эталоном оригинала. Индекс дополнительно проверяет каждый payload,
PTS/DTS, sync flag и end с прежним пределом **1 × 100 ns**.

Для VFR end sample определяется decode interval исходного MP4, а не всегда
полем `packet.duration` ffprobe. Отдельный независимый разбор исходной `stts`
подтвердил на Linux 119 отличий: например duration ffprobe=40 ms, исходная
stts и разность следующих DTS=80 ms. Индекс сохраняет исходный sample interval.
Это исправление эталона теста, не изменение работающего парсера и не ослабление
порога. Packet.duration ffprobe отдельно сравнивается для полного input/output.

| Сборка decoder fixture | Новые managed-профили | Прежние decoder cases | Exit |
|---|---:|---:|---:|
| Linux GCC + system libmpv | 4 | 11 | 0 |
| Linux ASan/UBSan + system libmpv | 4 | 11 | 0 |
| Windows MinGW + libmpv | 4 | 11 | 0 |
| Windows MSVC + libmpv | 4 | 11 | 0 |

Санитайзеры инструментируют собственный C++ parent/cache/decoder adapter;
готовая системная libmpv/FFmpeg не является санитайзерной сборкой. Отключение
leak checks или дополнительные suppressions для этого прогона не задавались.
На Linux также повторены 17 parent и 31 cache regression cases, exit CTest 0.

## Настоящий Classic DirectShow graph

Отдельный MSVC probe использует production `CHarvesterSession`, MP4 splitter
и software H.264 decoder с NV12 capture, без display renderer или audio device.
Пять новых B-frame сценариев: complete, growing с pause/update/resume, completed
seek, Stop и producer failure. Все driver/child exits 0. Completed/growing
дали по 600 кадров с точными SHA независимого FFmpeg NV12-эталона и проверенными
PTS/end; seek дал нужные 350 кадров. Stop/failure не выдаются за successful EOF.
Проверены прежний worker/reader, подтверждённый close и удаление cache.
Пять прежних no-B graph сценариев также прошли.

Это настоящее DirectShow-декодирование, но не запуск установленного Classic
через его UI/API. Qt проверяет production libmpv callback adapter, а не Window.

## Воспроизведение и сохранность

CTest: `progressive-managed-reorder`, timeout 300 s; регистрация проверена на
Linux и Windows. CLI `tests/progressive_managed_reorder_test.py` принимает
`--runner`, `--worker`, `--report-directory`; report directory должен быть новым.
Classic graph: прежний `tests/progressive_continuous_graph_test.py` получает
дополнительный `--reordered-video`; его старый вызов совместим.

Сохранён baseline CTest exit 8 при прежнем запрете reorder,
первые отказы теста preview/VFR и их исходники. Финальный SHA managed-теста
одинаков для всех четырёх сборок. Предварительные Windows прогоны с прежним
VFR-эталоном прошли, но не подменяют финальные.

Linux libmpv-проверки выполнялись только через `qt/tests/run_tests.sh` с частными
X11/D-Bus/PulseAudio. Windows native tests идут через SSH без запуска main EXE.
INI private Classic probe до первого запуска содержит ContextMenuPrompted=1
и ContextMenuEnabled=0. Все 17 защищённых файлов и 158 HKCU shell entries
совпали до/после; Harvester и Windows settings не менялись. Задач планировщика
или периодических проверок этот этап не создавал.

Далее: подключить профиль к controller и настоящим Player/Window/API, проверить
replay/замену/seek/lifecycle и объявлять capability только после общей приёмки
и поставки. Audio/AAC trim, общий remux, HD/4K, многочасовой stress и физический
A/V стенд остаются открытыми. Исторические Linux >2 s release и Classic
heartbeat-12 outliers не объявляются объяснёнными; оборудование и Wayland
не проверялись.
