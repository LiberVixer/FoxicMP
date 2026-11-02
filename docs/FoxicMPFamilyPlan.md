# Единый план развития FoxicMP

Актуальный [чеклист до завершения ТЗ](ProgressiveCompletionChecklist.md).

Рабочий план по запросу пользователя от 24 октября 2026 года.
**Эталон организации меню и настроек — Linux Qt** (уточнение пользователя
24 октября 2026). Windows Qt и Classic приводятся к этой структуре: общие
9 разделов меню, дерево категорий слева, вкладки справа, поиск и индикация загрузки.

**Уточнение после отката Classic, 24 октября 2026:** в Classic переносить только
порядок меню и настроек. Штатные страницы, локализацию, оформление, defaults,
горячие клавиши и поведение Classic сохранять. Новая организация не разрешает
переопределять их общими Qt-значениями. Текущий рабочий профиль не сброшен.

Начальное сравнение: [матрица возможностей трёх редакций](FoxicMPFamilyMatrix.md).
Этот план объединяет дальнейшую разработку семейства; [прежний Linux-план](LinuxParityPlan.md)
сохраняется как история реализации и ограничений. Старое «завершён» не заменяет
проверку соответствующего сценария на Windows Qt или Classic.

Подготовка интеграции с Harvester по новому ТЗ от 25 октября 2026 ведётся по
[отдельному плану](ProgressiveIntegrationPlan.md). Текущее наличие API v1 не
означает выполнение расширенной матрицы контейнеров/дорожек и общей приёмки.

## Общая цель

Один проект FoxicMP, три самостоятельно устанавливаемых приложения:

- Windows Classic — FoxicMP Classic, оранжево-синяя лиса.
- Windows Qt — FoxiqMP qt, оранжево-зелёная лиса, Windows QT.
- Linux Qt — FoxiqMP qt, оранжево-зелёная лиса, Linux QT.

Сближаем доступные пользователю операции, их названия, управление, общие
настройки и результат воспроизведения. Windows Qt и Linux Qt остаются одной
реализацией с платформенными адаптерами. Classic развивается в том же
репозитории, сохраняя MFC/DirectShow и свои специальные возможности.
Поведение, реализованное в одной редакции, проверяется на применимость к двум
другим в той же задаче; перенос работает в обе стороны.

Общий файл INI на три приложения не вводится. Предпочтения, история, библиотека,
устройства, блокировки экземпляров и регистрация ОС принадлежат своей установке.
Общий мастер переноса отложен пользователем; собственные операции импорта
и экспорта каждой редакции сохраняются.

## Порядок этапов

| Этап | Работа | Проверяемый результат | Начальное состояние |
|---|---|---|---|
| F0 | Инвентаризация трёх редакций и правила совместной работы | Матрица с исходниками, установленными версиями и отделением реализации от аппаратного подтверждения | Выполнено в этом сравнении; регрессионный прогон всех трёх сюда не входит |
| F1 | Общие команды и значения новых установок | 43 общие команды/defaults, адаптеры Qt/Classic, сценарий новых/старых профилей и Linux-эталон настроек | Qt 0.2.0: Linux и Windows целевые Qt-сценарии собраны; Classic сохраняет собственные defaults и клавиши |
| F2 | Поддержка Windows Qt на уровне общего Qt-приложения | Общие применимые Qt-тесты на обеих ОС, отдельный Windows Qt CI, корректные списки backend и системные медиакоманды | Частично: Win32 APPCOMMAND, сборка и 22/22 native-теста выпуска проверены; отдельный Windows Qt CI ещё не подключён |
| F3 | Общее меню и организация настроек | Согласованные 9 меню и категории настроек во всех редакциях, дерево слева; новые возможности Qt применены к Classic | Порядок меню и страниц Classic приведён к общей схеме; родные страницы, переводы и тема сохранены |
| F4 | Просмотр недокачанных файлов | Совместимый контракт загрузчика, Windows Qt адаптер, согласование Classic, единые сценарии буферизации/seek/отмены | Общий SDK/writer v1, release acknowledgement и Windows Qt legacy/provider; SDK 1.6 добавляет [сквозной v2 fMP4 H.264/AAC-LC](ProgressiveMediaV2.md), проверенный во всех трёх редакциях. SDK 1.7 выносит первичную подготовку IPC probe/start из UI; SDK 1.8 — update/complete и разбор индекса. SDK 1.9 — [IPC реестра и ограниченная очередь уничтожения Source](ProgressiveRetirement.md). SDK 1.10 — [фоновое legacy/provider opening](ProgressiveLegacyOpening.md) с отменой позднего применения. SDK 1.11 — [legacy maintenance вне UI metadata mutex](ProgressiveLegacyMaintenance.md), согласованный snapshot/index commit. SDK 1.12 — [Qt native teardown вне UI и безопасное Classic graph wait](ProgressiveEngineShutdown.md); SDK 1.13 — [асинхронное закрытие progressive Classic STA](ProgressiveClassicClose.md) с отложенными start/exit. Обычные/UI-owned Classic графы и GL cleanup ещё не закрыты. SDK 1.14 — [HEVC Main/Main10 + AAC-LC fMP4](ProgressiveHevc.md), SDK 1.15 — [AV1 Main 8/10-bit + AAC-LC fMP4](ProgressiveAv1.md) в schema 2 трёх редакций. SDK 1.16 — [VP9 Profile 0/2 + AAC-LC fMP4](ProgressiveVp9.md), 8/10-bit. SDK 1.17 — [Opus mono/stereo + все четыре видеокодека в fMP4](ProgressiveOpus.md). SDK 1.18 добавляет [отдельные WebM VP9/Opus дорожки](ProgressiveWebM.md). SDK 1.19 — [AV1 Main 8/10-bit + Opus в WebM](ProgressiveWebMAv1.md). SDK 1.20 — [смешанные пары fMP4/WebM](ProgressiveMixedContainers.md) в обоих направлениях. SDK 1.21 — [VP8 и Vorbis mono/stereo в WebM](ProgressiveVp8Vorbis.md). SDK 1.22 — [несколько аудиодорожек и поздние полные UTF-8 SRT](ProgressiveMultiTrack.md). SDK 1.23 — [перемотка Qt внутри подтверждённого участка](ProgressiveSeek.md) без прежнего лимита 10 секунд. SDK 1.24 — [изоляция ошибок optional SRT](ProgressiveOptionalSrt.md) в трёх редакциях. SDK 1.25 добавляет полный optional audio, SDK 1.26 — metadata/главы; SDK 1.27 исправляет их фактическое применение в UI и проверяет очистку, заголовок и смену сеанса. SDK 1.28 добавляет [выбранную частичную аудиодорожку](ProgressivePartialAudio.md), ожидание дозагрузки и возврат во время буферизации. SDK 1.29 изолирует обнаруженные ошибки активного optional audio с переходом на primary. SDK 1.30 добавляет [проверку SHA-256 подтверждённых блоков](ProgressiveIntegrity.md) для ресурсов с digest. SDK 1.31 добавляет [контракт сегментного timeline](ProgressiveSegments.md); SDK 1.33 включает ограниченный [segmented fMP4 playback](ProgressiveSegmentPlayback.md). Расширенное ТЗ ещё не выполнено; post-download — ограничение, не progressive fallback. YouTubeHarvester не менялся |
| F5 | Переносимые библиотека и предпочтения | Проверяемый импорт/экспорт плейлистов, избранного, истории и поддержанных общих настроек | Отложено пользователем; собственные форматы и профили сохраняются раздельно |
| F6 | Звук, субтитры, изображение и диагностика | Общие имена/диапазоны, Windows Qt два PCM-выхода, понятные платформенные исключения и фактический тракт | Второй Windows Qt выход реализован через отдельный WASAPI/libmpv PCM-декодер; физическая задержка не измерена |
| F7 | Общий цикл выпуска | Три артефакта из одного исходного commit, общий отчёт, версия продукта + номера движков | 0.2.0 опубликован: 3 portable-архива, MSI/EXE для обеих Windows-редакций, DEB Linux и SHA-256; общий CI и единый пакетный цикл ещё не подключены |

Каждый этап ведётся сразу по трём колонкам матрицы. Общий Qt-код изменяется
один раз. Для независимой задачи порядок может быть уточнён пользователем;
название этапа не даёт права отложить уже заказанное исправление другой редакции.
Две PCM-копии Windows Qt относятся к F6; задача progressive — к F4, поскольку
она имеет отдельные требования к растущим файлам и совместимости загрузчика.

Для F7 добавлен `tools/family_release_manifest.py`. Он создаёт атомарный JSON
манифест с commit, dirty-состоянием, версиями и SHA-256 трёх артефактов, не
изменяя установки и профили. Пример: `python3 tools/family_release_manifest.py
--output build/FoxicMP-family.json --qt-linux _bin/FoxicMPQt-linux-x64/foxicmp-qt`.


Для параллельной разработки добавлены `docs/FoxicMPFamilyCommands.json` и
`docs/FoxicMPFamilyCapabilities.json`, которые строятся/проверяются из общего
каталога `shared/familycommands.inc`. Тест `tests/family_contract_test.py`
проверяет уникальность IDs и наличие всех трёх редакций в матрице.

## F1: первые изменения

1. Составить общий каталог команд со стабильными идентификаторами:
   действие, русское/английское название, стандартная клавиша, смысл аргументов,
   доступность для файла/live/progressive. Сопоставить QAction Qt и numeric IDs
   Classic. Команды по IPC/Web не должны зависеть от переведённой подписи меню.
2. Выделить общий набор defaults для новых установок и платформенные добавки.
   Сейчас Windows Qt получает 617 полей снимка, Linux — профиль/fallback;
   Qt также имеет ранние клавиши buildActions и последующее classicKeys.
   Убрать несколько независимо редактируемых источников одинакового смысла.
3. Сохранить согласованное поведение: OSD выключено; положение окна запоминается;
   ограничение пропорций окна по кадру выключено; drag за видео левой кнопкой;
   клик play/pause и двойной клик fullscreen. Различать пропорции видеокадра и окна.
4. Взять эффективные стандартные клавиши Classic/Qt за базу: Ctrl+Q — файл,
   Ctrl+O — файл/URL, Alt+X — выход, Space — play/pause, O — настройки,
   Ctrl+Enter — fullscreen. Дополнительные назначения и настройки пользователя
   сохраняются; на сбросе/импорте явно показываются конфликты.
5. Зафиксировать одно стартовое отображение общих элементов по настроенной
   Windows-базе, включая скрытое верхнее меню. Переносить только независимые от ОС
   поля; не переносить Tahoma, идентификаторы аудиоустройств, координаты монитора
   и пути Windows в Linux. Существующие профили автоматически не нормализовать.
6. Для сценариев F1 сделать один набор ожидаемых результатов, исполняемый через
   адаптер каждой редакции: новый профиль, изменённый профиль, перезапуск,
   клавиши, drag/click и отсутствие изменений в соседней установке.

Результат F1 — одинаковый смысл стандартных действий и новых профилей,
а не совпадающее количество строк INI. Полный перенос всех 617 полей в Linux
или преобразование числового ID фильтра в произвольную строку libmpv не допускается.

## F1: результат Qt 0.2.0

В `shared/` добавлены платформо-нейтральные defaults и X-macro-каталог 43
команд. Qt использует его как fallback и удаляет только снимок shortcut-полей
из нового Windows-профиля; сохранённые и явно пустые keys не меняются.
Экспериментальный Classic-адаптер этих defaults/клавиш отменён: восстановлены
его исходные значения и создание команд из `9c76d2086`. Эталоном организации остаётся
Linux Qt: дерево слева, вкладки справа, поиск, 10 категорий и 40 страниц.

Общий семейный тест проверяет новый профиль, старый профиль с явными значениями,
изменение громкости, конфликт shortcut и отмену настроек. Linux Qt прогнал
`classic-interface`, `playback-controls`, `input-control`, `profile-archives`:
4/4 PASS. Windows Qt native suite: 21/21 PASS, включая этот сценарий. Classic
изолированные копии с новым и пользовательским INI: defaults, громкость, OSD,
положение окна, seek distances и сохранённая команда: PASS; рабочий профиль
Classic не использовался.

## F4/F6: текущий результат семейной синхронизации

Общий Qt-код версии 0.2.0 собирает растущие fast-start/fMP4 дорожки через
публикуемый JSON-контракт. Linux использует Unix-сокет, Windows Qt — локальный
named pipe, Classic — совместимый named-pipe адаптер к `HarvesterSession`;
YouTubeHarvester и его интерфейс не изменялись. Проверены stale revision, ожидание
новых байтов, отмена, завершение и переименование открытых файлов.

В Windows Qt второй PCM-выход создаётся отдельным libmpv-декодером с WASAPI
устройством и повторяет паузу, seek, громкость, mute, скорость и фильтры основного
выхода. Проверка подтверждает программную синхронизацию в тестовом backend;
реальные задержки двух звуковых устройств и HDMI/S/PDIF не измерялись.

## F3: результат интерфейсного этапа

Linux Qt остаётся эталоном организации; Windows Qt использует тот же Qt-код.
Экспериментальные англоязычные категории, поиск страниц и формальный индикатор
загрузки Classic отменены по запросу пользователя. Восстановлены все 26 штатных
страниц и переводы из ресурсов, без изменения установленного INI. Текущая
сборка Classic: 1.9.1.75 build 261024-1628.

В Classic верхнее меню теперь идёт в порядке «Файл / Воспроизведение / Вид / Навигация / Избранное / Справка», а порядок страниц внутри штатного дерева приведён к общей логике Qt
(общие страницы и интерфейс перед управлением), без замены родных страниц,
переводов или defaults. Перед изменением сохранена резервная копия EXE/INI и
исходников темы; хеши `SettingsTheme.cpp` и `PPageInternalFilters.cpp` совпадают
с копией. Нельзя считать это переносом Qt-интерфейса. Системные HWND из SSH-сессии
не доказывают наличие или отсутствие окна на интерактивном рабочем столе;
прежняя формулировка о причине «невидимого окна» была недоказанной.
Результаты восстановления: [отчёт](FoxicMPClassicRestore-2026-10-24.md).

По отдельному запросу пользователя окно настроек Classic теперь следует
собственной тёмной/светлой теме, без перестановки или замены страниц.
[Проверка темы и установки](FoxicMPClassicSettingsTheme-2026-10-24.md).
Последующее затемнение полос прокрутки, рамок, вкладок и списка встроенных
фильтров собрано, проверено и установлено в Classic.

Qt-редакции синхронизированы с Classic по панели управления: главы/файлы для
Previous/Next, двойные подсказки, правые действия Open/Frame/Stop/Audio/Subtitles
и единый hover/pressed стиль кнопок. Windows Qt получил Win32 APPCOMMAND для
play/pause, stop, next/previous, seek и громкости. Общая сборка установлена в
Linux Qt; Windows проверен отдельной сборкой и native toolbar test.

## F2: одинаковая база проверки Qt

В CMake общие progressive/audio/toolbar группы теперь подключаются и к Windows;
Windows имеет отдельные native checks. Сначала классифицировать существующие
тесты: общие, с подменяемым адаптером ОС, только Linux, только Windows.
Общие проверки перенести в обе сборки; не включать Linux PulseAudio/D-Bus/V4L2
тесты на Windows без соответствующего адаптера.

Добавить Windows Qt CI поверх существующих build.ps1/deploy.ps1 с проверкой
portable-пакета при системном PATH, без developer SDK в PATH. Проверять реальный
exit code процесса, в том числе после разрушения Qt/COM. В Qt убрать выдачу
Linux VA-API/VDPAU как подходящих Windows-backend; показывать только применимые
варианты и фактический fallback. Добавить системный адаптер медиакоманд Windows,
сохранив клавиши окна и отдельный Linux MPRIS.

## F3: интерфейс

За эталон организации взять текущую Linux Qt и её 9 разделов:
Файл / Воспроизведение / Видео / Аудио / Субтитры / Библиотека / Вид / Сервис /
Справка. Верхнее и контекстное меню используют одни действия и одинаковые
названия, включая состояния disabled/checked. Копировать структуру в Classic
постепенно; сохранить доступ к фильтрам, рендерерам и остальным специальным командам.

Категории настроек: Общие; Интерфейс и окно; Управление; Воспроизведение;
Видео; Аудио; Субтитры; Онлайн и сеть; Снимки и миниатюры; Обслуживание.
Слева дерево, справа вкладки, поиск параметров, немедленное открытие с индикацией
загрузки. Страницы конкретного движка размещать в подходящей категории.
Число вкладок между Qt-редакциями может различаться из-за доступности возможностей.
Для Classic действует уточнение выше: менять порядок, сохраняя родные страницы
и переводы. Qt-механизмы поиска/загрузки не объявлять реализованными в Classic.

Проверять светлую/тёмную темы, нижнюю строку, таймер, громкость, главы, длинные
названия, локализацию и HiDPI. Нативные системные рамки Windows/X11 сохраняют
допустимые платформенные различия. Новая организация не должна сбрасывать
цвета, клавиши или текущую библиотеку.

## F4: API просмотра во время скачивания

Действующий Linux API и Python-клиент остаются совместимыми; YouTubeHarvester
в этой работе не изменять. Описать общие операции запуска, публикации доступных
байтов, статуса, завершения, heartbeat и отмены. Разделить общую логику сеанса
и платформенные доступ к файлам/IPC/проверки владельца.

Windows Qt получает собственную реализацию вместо unavailable-заглушки:
растущие файлы с совместным чтением и Windows IPC с доступом владельца.
Classic получает совместимый адаптер к своему существующему Harvester-механизму;
не объявлять его текущий manifest/provider идентичным Linux API.
Действующие CLI, manifests и web-вызовы сохранять через адаптацию.

На всех трёх: MP4 fast-start и fMP4 в пределах поддержанного контракта,
отдельные видео/аудио, буферизация, догоняющая загрузка, seek только в доступные
диапазоны, EOF только после завершения, отмена, исчезновение загрузчика,
повторное подключение, ошибка без зависания. Сравнивать A/V одной методикой;
старый успешный headless-граф не доказывает физический звук.

## F5–F6: данные и мультимедиа

Обмен библиотекой начать с уже поддерживаемых M3U8/MPCPL и набора образцов;
для закладок/истории нужен версионируемый нейтральный формат. Импорт показывает,
что перенесётся, что не поддержано и как преобразуются пути Windows/Linux.
Работа исходного плеера и исходные файлы при импорте не меняются.

В звуке сравнить выбор дорожек/выхода, задержку, отсутствие mute после перезапуска,
каналы, нормализацию и одновременные PCM-выходы. В субтитрах — SRT/ASS, язык,
встроенные шрифты, задержку, размещение и сохранение. В видео — пропорции,
crop/pan, поворот, цвет, скейлеры и фактический software/hardware тракт.

DirectShow DLL, граф фильтров, HLSL и renderer-specific очереди остаются
возможностями Classic. У Qt — GLSL/libmpv и платформенные backend. Общая задача
эффекта может иметь разные реализации; неподдержанная функция явно обозначается.
Qt RGBA8 не называть полноценным 10-bit/HDR passthrough. Общую диагностику
дополнить фактическими декодерами, renderer, AO, частотой, каналами и A/V.

## Как вести дальнейшие изменения

Для каждой функции вести три состояния: Classic Windows / Qt Windows / Qt Linux,
её общий ожидаемый результат и конкретные допустимые различия. Сделано означает
подключённый интерфейс/CLI, сохранение параметров, проверенный сценарий и пакет;
возможность движка сама по себе не закрывает задачу.

Для общего изменения:

1. Реализовать общий Qt-путь и применимую Classic-часть в одной задаче.
2. Проверить изменённые сценарии в обеих Qt-сборках и Classic-адаптере; затем
   выполнить необходимую регрессию. Не выдавать тест на одной ОС за проверку другой.
3. Записать реальные результат, commit, версию, платформу, фактические renderer/AO,
   применённые допуски и оставшиеся ограничения. Отключённый сценарий — не PASS.
4. Обновить матрицу, заметки выпуска и установленные копии в пределах задачи.
   Каждую новую проверенную Linux-версию устанавливать с копией прежнего EXE,
   атомарной заменой и сохранением INI/личных файлов. Не прерывать просмотр.
5. Подготовить три пакета одного цикла с manifest исходного commit и зависимостей.
   Общий номер продукта вводить вместе с общим выпуском; номера движков/сборок
   сохранять в диагностике. Нынешние 1.9.1.75 и 0.1.73 не менять ради видимости паритета.

Документация без изменения программы не требует новой версии и переустановки.
Сборки, которые не изменились, не нужно повторно выпускать с искусственным bump.
Для платформенной функции допускается отдельный выпуск, явно отмеченный в общем
журнале; общий функциональный рубеж считается достигнутым только после проверки
применимых сценариев во всех трёх редакциях.

## Набор общей приёмки

Одна медиаматрица с одинаковыми файлами/хешами: исправные и повреждённые
контейнеры, только звук, многоканальный звук, две дорожки, SRT/ASS, главы,
кириллица/длинные пути, HTTP и growing-сценарии. Проверять декодирование и
состояние, видимый кадр, измеримый звук, seek, естественный EOF и восстановление
после ошибки раздельно. Для seek обычных файлов исходный ориентир — имеющийся
допуск 150 мс; A/V после buffering/seek — имеющаяся методика до 100 мс с учётом
её неопределённости. Для иных источников ограничения фиксируются явно.

UI-эталоны и данные создавать на отдельных тестовых профилях. Linux — отдельные
Xvfb/D-Bus/PulseAudio; Windows — отдельные копии/профили, при необходимости
одноразовый интерактивный запуск. Не менять глобальный микшер и не выключать
рабочую машину тестом. Null renderers подтверждают разбор/декодирование,
но не изображение, реальный звук или качество синхронизации.

## Исключения по действующим решениям пользователя

Дисковые меню, аналоговое ТВ и физические устройства отложены. Wayland не
проверяем. Физических ресиверов/HDMI/S/PDIF/нескольких дисплеев на стенде нет:
их возможности не объявлять подтверждёнными. Эти задачи не возобновляются
автоматически в рамках сближения. Профили MPC/MPC-BE и
`C:\FoxBroxer\Prg\MPMY` не изменять. Повторяющиеся проверки по времени не создавать.

SDK 1.32 добавляет [staging и demux одного fMP4-сегмента](ProgressiveSegmentStaging.md) через `resourceProbe`; это диагностика, не segment playback. Harvester не меняется.

SDK 1.33 добавляет [ограниченное воспроизведение цепочки fMP4 H.264/AAC-LC](ProgressiveSegmentPlayback.md) через общий I/O трёх редакций. TS, live, смена codec/init и произвольный HLS/DASH остаются за границей этого профиля; Harvester не менялся.

SDK 1.34 добавляет [HEVC Main/Main10 в сегментном fMP4](ProgressiveSegmentHevc.md) с AAC-LC для всех трёх редакций. Требуется явная capability профиля; TS/live и смена конфигурации остаются за границей этого этапа.

SDK 1.35 добавляет [AV1 Main 8/10-bit в сегментном fMP4 с AAC-LC](ProgressiveSegmentAv1.md) для всех трёх редакций. VP9/Opus в сегментном профиле, TS/live и смена конфигурации остаются отдельными этапами.

SDK 1.36 добавляет [VP9 8/10-bit и Opus mono/stereo в сегментном fMP4](ProgressiveSegmentVp9Opus.md) для всех трёх редакций. Следующий этап — длинные ролики без упора в 64 ресурса; WebM/TS, muxed A/V, смена конфигурации и live остаются последующими этапами.

30 октября: начат этап длинных роликов — [общая модель постраничного каталога](ProgressiveSegmentCatalog.md), до 8192 ресурсов, проверена отдельно от playback. Далее: безопасный локальный адаптер страниц, ограничение открытых файлов/leases, подключение к движкам и длинные A/V-тесты всех трёх редакций. Публичный API и установленные сборки остаются на проверенном SDK 1.36; увеличение лимита playback ещё не объявляется готовым.

Следующий подготовительный этап завершён: [локальный адаптер страниц](ProgressiveLocalCatalog.md), проверки на Linux, Windows MinGW и MSVC. Далее — ограниченные media handles/reader leases с проверкой повторного открытия, затем bridge/движки и длинный playback. Ни страницы, ни модель пока не объявлены публичной capability.

Подготовлен [внутренний кэш media resources](ProgressiveCatalogCache.md): до восьми открытых файлов, LRU, SHA-256 обязателен, release/forget после фактического закрытия. Проверки одинаковы для GCC/MinGW/MSVC; дальше — интеграция `Fmp4V2Bridge`, обе Qt-сборки и Classic, capability/SDK и длинные A/V-тесты. Действующий playback всё ещё использует прежний inline-режим.

SDK 1.37 подключает [каталог к playback всех трёх редакций](ProgressiveCatalogPlayback.md). Проверены 90-секундные записи и 182–183 ресурса, pause/seek/update/reopen/release. Далее по порядку — WebM/TS-сегменты, затем muxed A/V, смена конфигурации/live и реальные источники с многочасовой A/V-приёмкой. Оптимизация старта большого полностью готового каталога остаётся ограничением этого этапа; Harvester не меняется.

SDK 1.38 добавляет [сегменты WebM VP8/VP9/AV1 с Opus](ProgressiveWebmSegments.md) и отдельную capability `paged-webm-v1` во всех трёх редакциях. Проверки охватывают 20 секунд, пять видеопрофилей и три варианта каталога; многочасовая A/V-приёмка этим не закрывается. Следующий этап — MPEG-TS, затем muxed A/V, смена конфигурации/live и реальные источники. Segmented Vorbis требует отдельного профиля с состоянием между сегментами. Harvester не меняется.

Подготовлен [общий транспортный парсер MPEG-TS](ProgressiveTsTransport.md): PAT/PMT/PES, CRC, continuity, PCR/PTS/DTS и bounded payload ranges. Проверен GCC/MinGW/MSVC и ASan/UBSan. Это внутренний этап без TS playback capability. Далее — AVC/AAC access units, конфигурация и clock/index, ResourceSource/staging, затем Classic и обе Qt-сборки с декодированием и обновлением SDK. Действующий SDK 1.38 и установленные плееры не меняются.

Подготовлен [AVC/AAC access-unit и clock/index слой TS](ProgressiveTsAccessIndex.md): fixed-rate AVC Baseline/Main/High, ADTS AAC, SPS/PPS/rate/channel invariants, envelope и непрерывность между сегментами, IDR/audio seek и readiness по byte prefix. GCC/MinGW/MSVC и sanitizers проверены отдельно от плееров. Далее — ResourceSource/staging и виртуальное чтение, затем декодеры/управление трёх редакций и новая capability/SDK. Entropy decoding, VFR и реальные источники этим не закрываются; SDK 1.38 и установки остаются прежними.


Подготовлен [TS ResourceSource/staging и виртуальный поток](ProgressiveTsResourceStream.md): обязательный SHA, bounded cache восьми файлов, непрерывный готовый префикс, inline/paged catalog, byte/sample read, seek и last-owner leases. Проверены 242 ресурса и два реальных AVC/AAC-профиля: 438 packet hashes и внешнее FFmpeg-декодирование совпадают с исходными TS. Ошибки metadata до публикации допускают retry; ошибки ресурса/codec после публикации закрывают источник до reopen. Далее — bridge/engine adapters Classic и обеих Qt-сборок с buffer/seek/update/complete/release, затем capability/SDK. SDK 1.38 и пользовательские установки не меняются; Harvester не затрагивается.


SDK 1.39 подключает [сегментный MPEG-TS AVC/AAC](ProgressiveTsPlayback.md) ко всем трём редакциям через общий виртуальный fMP4 без перекодирования. Обязательны SHA-256, отдельные video/audio, внешний PAT/PMT init и строгий VOD-профиль; каталог согласуется отдельно как `paged-mpegts-v1`. Проверяются обычный manifest, каталог и отрицательный преролл, pause/backward/forward seek, tail waiting и complete/release. Harvester не меняется. Далее — muxed A/V, смена конфигурации/live и реальные источники с длительной A/V-приёмкой; этот выпуск не объявляет их готовыми.


SDK 1.40 добавляет [совместные MPEG-TS AVC/AAC сегменты](ProgressiveMuxedTsPlayback.md) для Classic и обеих Qt-сборок: общий init/media resource, два logical view, виртуальный fMP4, общий SHA/cache/leases и отдельный `paged-muxed-mpegts-v1`. Старые профили sharing не допускают. Далее — один растущий muxed-файл без сегментации, muxed fMP4/WebM, смена конфигурации/live и реальные источники с длительной A/V-приёмкой. Harvester не меняется.

SDK 1.41 расширяет F4 [одним растущим TS VOD](ProgressiveGrowingTs.md):
две required AVC/AAC проекции поверх одного SHA-защищённого префикса,
ожидание partial packet/PES/GOP и виртуальный MP4 для всех трёх редакций.
Далее — muxed fMP4/WebM и отдельная приёмка реальных источников/live.

SDK 1.42 расширяет F4 [одним shared fMP4 AVC/AAC VOD](ProgressiveMuxedFmp4.md):
projected moov/traf, общий mdat и physical lease, SHA и ожидание полного fragment.
Следом — muxed WebM и дополнительные кодеки muxed fMP4; live и реальная
длительная A/V-приёмка остаются отдельными этапами.

SDK 1.43: [shared growing WebM VP9/Opus](ProgressiveMuxedWebm.md) и общий
scalar bridge с fMP4. Далее — расширение muxed codec matrix, затем
конфигурация/live и отдельная реальная длительная A/V-приёмка.

SDK 1.44: [shared WebM AV1/Opus](ProgressiveMuxedWebmAv1.md), Main 8/10-bit,
с отдельным согласованием и сохранением VP9. Следом — VP8/Vorbis в shared
WebM и расширение muxed fMP4, затем смена конфигурации/live и длительная
приёмка реальных источников.

SDK 1.45: [shared WebM VP8/Opus](ProgressiveMuxedWebmVp8.md) с отдельной
capability, прежними VP9/AV1 и проверкой первого displayed keyframe. Далее —
Vorbis в shared WebM, дополнительные кодеки muxed fMP4, смена конфигурации/live
и длительная приёмка реальных источников.

SDK 1.46: [Vorbis в shared WebM](ProgressiveMuxedWebmVorbis.md), пары с
VP8/VP9/AV1, state между Clusters, CodecDelay и padding. Следом — HEVC/AV1
и остальные кодеки muxed fMP4, смена конфигурации/live и длительная реальная
A/V-приёмка. Harvester не меняется.

SDK 1.47 добавляет [HEVC Main/Main10 + AAC-LC в одном растущем fMP4](ProgressiveMuxedFmp4Hevc.md).
Прежний AVC mode сохранён; HEVC согласуется отдельно через `muxedPlayback.fmp4.hevc.mode`.
Следующий шаг [чеклиста](ProgressiveCompletionChecklist.md) — AV1/AAC в shared fMP4;
полное ТЗ, real-source и длительная физическая A/V-приёмка остаются открытыми. Harvester не меняется.

SDK 1.48: [AV1 Main 8/10-bit + AAC-LC в одном растущем fMP4](ProgressiveMuxedFmp4Av1.md),
отдельный `muxedPlayback.fmp4.av1.mode`; прежние AVC/HEVC сохранены.
Далее по [чеклисту](ProgressiveCompletionChecklist.md) — shared fMP4 VP9/AAC,
затем допустимые Opus пары. Остальное ТЗ и длительная реальная A/V-приёмка остаются открытыми.
Harvester не меняется.

SDK 1.49: [VP9 Profile 0/2 8/10-bit + AAC-LC в одном растущем fMP4](ProgressiveMuxedFmp4Vp9.md),
отдельный `muxedPlayback.fmp4.vp9.mode`; прежние AVC/HEVC/AV1 сохранены.
Далее по [чеклисту](ProgressiveCompletionChecklist.md) — допустимые shared fMP4 Opus пары.
Остальное ТЗ, реальные источники и длительная физическая A/V-приёмка остаются открытыми.
Harvester не меняется.

SDK 1.50: [Opus mono/stereo с H.264/HEVC/AV1/VP9 в общем растущем fMP4](ProgressiveMuxedFmp4Opus.md),
`muxedPlayback.fmp4.opus.mode` и exact pair profiles, исходные dOps/edit/pre-skip сохранены.
Ограниченная матрица shared fMP4 AAC/Opus закрыта; далее [пункт 2 чеклиста](ProgressiveCompletionChecklist.md) —
общий backend demux и локальная подготовка. Полное ТЗ и реальная длительная A/V-приёмка остаются открытыми.
Harvester не меняется.

SDK 1.51: [ограниченная FFmpeg AVIO диагностика ресурсов](ProgressiveBackendDemux.md)
в Classic и обеих Qt-редакциях. Только подтверждённые ranges с SHA-256, отдельный
worker, bounded packets/timestamps и прежние leases/update/cancel. Это первый этап
пункта 2 чеклиста; новый playback/remux и итоговая классификация ещё не реализованы.
Harvester не меняется.

SDK 1.52: [bounded copy-remux MP4-фрагмента в памяти](ProgressiveRemuxStage.md),
`backendRemuxProbe` и `ResourceProbe.start_remux`, общий код трёх редакций.
Точный AAC trim сохраняется обычным MP4; фрагмент может быть подготовлен до complete
при поддержанном профиле. Это preparation-only, без нового playback/непрерывного remux.
Оставшаяся часть пункта 2 чеклиста открыта. Harvester не меняется.


SDK 1.53: [явное воспроизведение подготовленного RAM MP4-фрагмента](ProgressivePreparedPreview.md)
в трёх редакциях, capability `preparedRemuxPreview`, helper `RemuxPreview`.
Прежняя preparation-only capability не меняется. Проверены pause/seek/clip EOF,
восстановление запроса и освобождение читателей. Далее по пункту 2 чеклиста —
непрерывная ограниченная подготовка с ожиданием/продолжением и расширенная
container/codec matrix. Полное ТЗ остаётся открытым. Harvester не меняется.


SDK 1.54: [весь растущий обычный fast-start MP4 H.264/AAC-LC](ProgressiveIndexedMp4.md),
`schema2Playback.muxedPlayback.mp4`, одинаковый native-progressive путь трёх редакций.
Полный индекс из head moov, проекция подтверждённых sample payloads без перекодирования,
ожидание/update/resume с прежним readerId. Ограничение RAM clip 1.53 не применяется
к новому профилю. Общий backend remux, moov-at-tail, остальные контейнеры/кодеки,
live и длительная реальная A/V-приёмка остаются открытыми. Harvester не меняется.


SDK 1.55: [обычный MP4 H.264/AAC-LC с индексом в конце](ProgressiveTailMp4.md),
`mp4-tail` и отдельная capability `muxedPlayback.mp4.tailIndex`. Metadata-only
`ResourceProbe.start_index` возвращает advisory ranges без скачивания/старта.
Один подтверждённый media prefix + один metadata range вне mdat; hole payload
не читается. Прежние head MP4, RAM preview и остальные профили сохранены.
[Проверки трёх редакций](ProgressiveTailMp4Validation-2026-11-01.md).
Общий FFmpeg remux, произвольные sparse media, другие контейнеры/кодеки, live
и длительная реальная A/V-приёмка остаются в чеклисте. Harvester не меняется.


SDK 1.56: [перемотка активной сессии после complete](ProgressiveCompletedSeek.md),
capability `completedSourceSeek`. Qt переоткрывает декодер на том же pinned source,
когда прежний потоковый decoder cache ограничивал дальний seek; Classic сохраняет
штатное позиционирование. Reader, ручная пауза, скорость, громкость/mute и основная
аудиодорожка сохранены; status дополнен `controls`. [Проверки трёх редакций](ProgressiveCompletedSeekValidation-2026-11-01.md).
Это ограниченный H.264/AAC-LC профиль; общий remux, прочие форматы и длительная
реальная приёмка остаются в чеклисте. Harvester не меняется.


SDK 1.57: [самостоятельные AAC-LC audio-only и H.264 video-only в fMP4](ProgressiveStandalone.md),
один реальный required track/resource, отдельная capability `schema2Playback.standalone`.
Тип дорожки отделён от native slot; буферизация не ждёт отсутствующий поток.
Проверены mono/stereo, VFR/B-frames, неизвестная длина, pause/seek/update/complete,
release и [совместимость трёх редакций](ProgressiveStandaloneValidation-2026-11-01.md).
Общая codec/container matrix и оставшееся ТЗ открыты. Harvester не меняется.

SDK 1.58: [самостоятельное Opus mono/stereo в растущем fMP4](ProgressiveStandaloneOpus.md),
профиль `standalone-fmp4-opus` внутри `schema2Playback.standalone`.
Один настоящий audio track/resource, 48 kHz, mapping family 0, проверка dOps/pre-skip/edit list;
старые AAC/H.264 и парные Opus профили сохранены. [Проверки каждой редакции](ProgressiveStandaloneOpusValidation-2026-11-01.md).
Следом — самостоятельные WebM/Opus/Vorbis и дальнейшая матрица из чеклиста. Harvester не изменён.

SDK 1.59: [самостоятельные Opus/Vorbis mono/stereo в растущем WebM](ProgressiveStandaloneWebm.md),
отдельная capability `schema2Playback.standalone.webm` и профили
`standalone-webm-opus` / `standalone-webm-vorbis`. Один настоящий audio track/resource;
тип slot 0 и аудиоперемотка Classic исправлены без изменения парных потоков.
[Результаты трёх редакций](ProgressiveStandaloneWebmValidation-2026-11-01.md).
Далее — standalone WebM video, остальные codec/container profiles и пункты чеклиста; Harvester не изменён.

SDK 1.60: [самостоятельное WebM-видео VP8/VP9/AV1](ProgressiveStandaloneWebmVideo.md),
отдельная capability `schema2Playback.standalone.webmVideo` и точные profiles.
Шесть runtime profiles каждой редакции: VP8, VP9/AV1 8/10-bit, 25 и 30000/1001 fps;
один настоящий video track/resource без audio. [Проверки](ProgressiveStandaloneWebmVideoValidation-2026-11-01.md).
Standalone WebM audio/fMP4 сохранены. Далее — прочие standalone codec/container profiles
и пункты общего чеклиста. Harvester не изменён.

SDK 1.61: [самостоятельное HEVC/AV1/VP9-видео в растущем fMP4](ProgressiveStandaloneFmp4Video.md),
точные standalone profiles, шесть 8/10-bit и fractional runtime rows каждой редакции.
Штатные hvcC/av1C/vpcC validators и один настоящий video track/resource без аудио;
[проверки](ProgressiveStandaloneFmp4VideoValidation-2026-11-01.md).
WebM и прежние fMP4 profiles сохранены. Далее — оставшаяся codec/container matrix
и пункты общего чеклиста. Harvester не изменён.

SDK 1.62: [самостоятельное FLAC-аудио в растущем fMP4](ProgressiveStandaloneFlac.md),
восемь mono/stereo × 16/24-bit × 44.1/48-kHz runtime profiles на редакцию,
строгая dfLa/STREAMINFO и PCM oracle после чтения через bridge;
[проверки](ProgressiveStandaloneFlacValidation-2026-11-01.md).
Прежние AAC/Opus/video сохранены. Raw `.flac`, другие audio containers,
многоканальные layout/rates и остальные пункты чеклиста остаются открытыми. Harvester не изменён.

SDK 1.63: [самостоятельный растущий native FLAC](ProgressiveRawFlac.md),
один настоящий audio track/resource; fixed-block mono/stereo, 16/24-bit,
44.1/48 kHz. Границы кадров, CRC-8/CRC-16 и SHA-256 проверяются до публикации
в виртуальный fMP4; исходные compressed frames сохраняются без перекодирования.
[Проверки трёх редакций](ProgressiveRawFlacValidation-2026-11-01.md).
Остальные audio containers, layouts/rates/depths, variable blocks, live и
реальная интеграция Harvester остаются в общем чеклисте. Harvester не изменён.

SDK 1.64: [самостоятельный растущий MPEG-4 ADTS AAC-LC](ProgressiveAdts.md),
mono/stereo, 44.1/48 kHz, без CRC, один raw data block/frame. AAC payload
передаётся через виртуальный MP4 без перекодирования, со строгими frame bounds,
неизменной configuration и ожиданием неполного header/payload.
[Проверки трёх редакций](ProgressiveAdtsValidation-2026-11-01.md).
FLAC использует общий scalar audio bridge и MP4 projection с сохранением
своего контракта и отдельными regression checks. Harvester не изменён;
остальные ADTS/audio formats и общая приёмка остаются в чеклисте.

SDK 1.65: [самостоятельный растущий MP3 MPEG-1 Layer III](ProgressiveMp3.md),
CBR/VBR mono/stereo, 44.1/48 kHz без CRC. Bounded ID3v2.3/v2.4/ID3v1,
проверка frame bounds и reservoir references; 250 ms seek preroll в Qt/Classic.
[Проверки трёх редакций](ProgressiveMp3Validation-2026-11-01.md).
Полные compressed frames сохраняются в виртуальном MP4 без перекодирования.
Xing/gapless, прочие MPEG audio profiles и общий чеклист остаются открытыми.
Harvester не изменён.

SDK 1.66: [MP3 с Xing/Info/VBRI](ProgressiveMp3Indexed.md), отдельный
`standalone-mp3-indexed-v1` с encoded-sample timeline. Счётчики проверяются
на growing/complete, non-audio index frame пропускается; raw encoder
delay/padding сохраняется, `gaplessTrimApplied=false` объявлен явно.
[Проверки трёх редакций](ProgressiveMp3IndexedValidation-2026-11-01.md).
Прежний MP3-профиль сохранён. Gapless decoder/output adapters, реальный
VBRI corpus и прочие пункты чеклиста открыты; Harvester не изменён.

Следующий gapless-этап начат: [PCM-эталон и порядок адаптеров](ProgressiveMp3GaplessReference.md).
По 128 проверок Linux/Windows, включая цифровой libmpv output и точный
`atrim`/seek. Это тестовый стенд, без изменения API/воспроизведения SDK 1.66;
подключение к Qt progressive controller и Classic PCM adapter открыто.

Реализован [внутренний Qt PCM adapter](ProgressiveQtPcmWindow.md): по 72
проверки настоящего `Player` Linux/Windows, пользовательская обработка,
точный trim/seek и очистка на stop/replacement. Следуют привязка audible
времени в progressive controller и независимый Classic PCM/output adapter.
Публичный gapless пока не объявляется, API/SDK 1.66 сохраняется.

SDK 1.67: [LAME Info Tag CRC / PCM trim plan](ProgressiveMp3Tag.md),
единое ядро трёх редакций. CRC проверяет header/metadata, не звук целиком;
недостоверная metadata не даёт trim plan и не ломает прежний encoded playback.
[Native/runtime проверки](ProgressiveMp3TagValidation-2026-11-01.md),
включая настоящий libmp3lame encoder, corrupt tags и ASan/UBSan.
Публичного gapless playback ещё нет: controller и Classic PCM adapter открыты.
Harvester не изменён.

SDK 1.68: [gapless MP3 в Windows/Linux Qt](ProgressiveMp3Gapless.md),
отдельный `standalone-mp3-gapless-v1` с полезной звуковой шкалой,
проверенным tag CRC, точным PCM trim/seek и ожиданием writer complete.
[Результаты](ProgressiveMp3GaplessValidation-2026-11-02.md): по 31 новым native
checks, четырём growing profiles и 16 controller PCM windows на Qt ОС.
Classic явно отказывает новому профилю без reader, сохраняя прежнее воспроизведение;
его output adapter остаётся открытым. Общие capabilities прежние, Qt-дополнение
описано отдельно. Linux обновлён с backup и сохранением INI. Harvester не изменён.

Внутренний [PCM-адаптер Classic](ProgressiveClassicPcmWindow.md) проверен
через настоящий CFFAudioDecoder: 40 checks, 16 точных окон, flush/replacement
и отказ при усечённом PCM. Scalar: 1737 checks MSVC/Linux, ASan/UBSan Linux.
Публичный gapless Classic пока отключён: подключение graph/controller,
точное re-arm на seek и capture после DirectShow graph остаются открытыми.
Qt/SDK 1.68, пользовательские профили и установки не меняются.

[PCM-окно Classic подключено к внутреннему DirectShow-графу](ProgressiveClassicPcmGraph.md):
точный frame/sample preroll, передача плана после startup flush, применение
после discontinuity и повторное вооружение на seek. По 16 PCM capture windows,
четырём growing-профилям и проверке несовместимого downstream без вывода звука.
Публичная capability Classic остаётся выключенной до выбора decoder/status/UI
и проверки пользовательской обработки; Qt и SDK 1.68 не меняются.

SDK 1.69: [gapless MP3 через обычный API Classic](ProgressiveClassicMp3Gapless.md).
Проверенный внутренний decoder подключается независимо от codec merits;
статус отражает применение PCM-окна, в том числе после paused seek/Stop/cancel.
Четыре growing profiles Classic и 12 запусков Stop/cancel/reopen с отключённым
обычным приоритетом internal MP3 прошли проверку. Повторены четыре Qt profiles
и 16 controller PCM windows на каждой ОС; SDK — по 68 tests Linux/Windows.
Общий manifest сохранён, Classic/Qt additions описаны отдельно. Полная audio
matrix, длительная A/V-приёмка и реальная Harvester-интеграция остаются открытыми.

SDK 1.70: [растущий RIFF/WAVE PCM16](ProgressiveWav.md), mono/stereo
44.1/48 kHz, отдельный профиль с известными RIFF/data sizes. Проекция без
перекодирования сохраняет PCM bytes; неполные packets ждут update.
По 55 native checks Windows/Linux и 55 ASan/UBSan checks, четыре growing
profiles каждой редакции; повторены MP3 gapless/ADTS native и Classic playback.
SDK — по 69 tests на ОС. Linux обновлён атомарно с backup и сохранением INI;
Harvester и установленные Windows-плееры не менялись. RF64/float/24-bit,
прочие audio profiles и полная приёмка остаются открытыми.
