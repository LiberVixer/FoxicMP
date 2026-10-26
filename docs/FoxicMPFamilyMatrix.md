# FoxicMP: сравнение трёх редакций

Срез исходников и проверок: 25 октября 2026 года, Qt 0.2.0.
Эталон организации меню и настроек — **Linux Qt**, включая перенос в Classic.
Основа дальнейшей работы: [общий план](FoxicMPFamilyPlan.md).

## Что именно сравнивается

| Редакция | Движок | Проверенная сборка выпуска 0.2.0 | Отдельный профиль |
|---|---|---|---|
| FoxicMP Classic, Windows | MFC / DirectShow, код MPC-BE | 1.9.1.75 build 261025-1303 | `C:\FoxBroxer\Prg\FoxicMP` |
| FoxiqMP qt, Windows | Qt Widgets / libmpv | 0.2.0 (сборка проверена) | `C:\FoxBroxer\Prg\FoxicMPq` |
| FoxiqMP qt, Linux | Qt Widgets / libmpv | 0.2.0 (сборка проверена) | `_bin/FoxicMPQt-linux-x64` |

Версии выпуска проверены в сборочных/portable-копиях Windows через SSH;
Linux — через `--version`. Это версии пакетов, а не утверждение об обновлении
Windows-установок. Linux-копия обновлена до 0.2.0 с сохранением INI.
Профили и системные настройки Windows при выпуске не менялись.
Classic восстановлен после экспериментальной семейной синхронизации; текущий
профиль не менялся. Уточнение пользователя: в Classic переносить только порядок
меню/настроек, сохраняя собственные переводы, страницы, defaults и управление.
Дополнительно исправлены 79 команд контекстного запуска, ошибочно ведущих
к тестовой копии; реальный ролик с 14 главами и сохранение размера проверены.
См. [отчёт восстановления](FoxicMPClassicRestore-2026-10-24.md).

**Есть** означает подключённую функцию в исходниках приложения. Это не новая
проверка всех её сценариев на трёх ОС. **Частично** означает конкретно описанную
границу. **Нет** относится к названной функции/адаптеру, а не возможностям ОС.
Физический звук, GPU/HDR и оборудование не выводятся из наличия галочки.

## Источники и воспроизведение

| Возможность | Windows Classic | Windows Qt | Linux Qt | Что выравнивать |
|---|---|---|---|---|
| Локальные видео/аудио, папки, drag-and-drop | Есть | Есть | Есть | Один набор образцов и одинаковое поведение очереди |
| MP4/MKV/AVI/MOV/WebM, распространённые кодеки | Есть, свои декодеры/фильтры | Есть, libmpv/FFmpeg | Есть, libmpv/FFmpeg | Проверять фактические дорожки и вывод; не обещать все кодеки по расширению |
| HTTP(S), сетевые медиапотоки | Есть | Есть | Есть | Одинаковые ошибки, таймауты и правила повторного открытия |
| Страницы онлайн-видео и выбор качества | YouTube / YT_DLP, собственные настройки | Общие Qt / yt-dlp | Общие Qt / yt-dlp | Одни публичные сценарии YouTube/Rutube/VK, качество, звук и отмена |
| Онлайн-плейлисты | Есть | Есть | Есть | Лимиты, порядок, добавление и пропуск ошибок |
| Воспроизведение во время скачивания | Named-pipe v1 и подтверждение release | Named-pipe v1, native legacy/provider, подтверждение release | Provider, Unix v1, подтверждение release | Общий SDK/writer; две MP4/M4A H.264/AAC дорожки. Остальная матрица [нового ТЗ](ProgressiveIntegrationPlan.md) не выполнена; Harvester не меняется |
| Один контракт вызова загрузчиком | Есть v1 Windows named-pipe адаптер | Есть v1 Windows named-pipe API | Есть v1 Unix API | Общие JSON-команды и совместимые IPC-адаптеры |
| Проверка расширенного manifest v2 | Общий validate, без открытия | Общий validate, без открытия | Общий validate, без открытия | [Модель и границы](ProgressiveManifestV2.md), [результаты](ProgressiveModelValidation-2026-10-25.md). Есть ограниченный playback v2 H.264/AAC-LC fMP4; расширенные track/live scopes не подключены |
| Учёт reader по закреплённым файлам | Opt-in player-owned v1, IPC и OS receipts | Тот же runtime | Тот же runtime | [Reader leases](ProgressiveReaderLeases.md), [проверки](ProgressiveReaderValidation-2026-10-25.md); ресурсы v2 и legacy/provider не объявляются этим покрытыми |
| Подтверждённые диапазоны v2, I/O-слой | Общий ResourceSource и resourceProbe | Тот же код | Тот же код | [Контракт](ProgressiveResourceIo.md), [проверки](ProgressiveResourceValidation-2026-10-26.md). Leases ресурсов и байтовое чтение; отдельно подключён [fMP4 playback v2](ProgressiveMediaV2.md), универсальный remux отсутствует |
| Медиапрофиль v2: отдельные fMP4 H.264/AAC-LC | ResourceSource → DirectShow MP4 | ResourceSource → libmpv | Тот же Qt-код | [Профиль и границы](ProgressiveMediaV2.md), [сквозные проверки](ProgressiveMediaValidation-2026-10-26.md); не вся матрица ТЗ |
| Первичная подготовка progressive API вне UI | Worker → UI commit | Worker → UI commit | Тот же Qt-код | [SDK 1.7](ProgressivePreparation.md): probe/start, отмена позднего результата; [SDK 1.8](ProgressivePublication.md) добавляет update/complete и отделяет I/O от UI metadata mutex |
| Реестр progressive readers и уничтожение Source вне UI | IPC worker + FIFO cleanup | IPC worker + FIFO cleanup | Тот же Qt-код | [SDK 1.9](ProgressiveRetirement.md): 32 Source, ≤2 cleanup-workers; release только после закрытия файлов; renderer/graph shutdown отдельно |
| Открытие legacy/provider источника вне UI | Worker → UI graph | Worker → UI mpv | Тот же Qt-код | [SDK 1.10](ProgressiveLegacyOpening.md): один слот, 5 с до отмены применения; native I/O не прерывается принудительно, старые refresh/notify locks отдельно |
| Legacy refresh без файлового I/O под UI metadata mutex | Native checks/index → commit | Worker checks/publish → commit | Worker checks/index → commit | [SDK 1.11](ProgressiveLegacyMaintenance.md): проверка усечения и при неизменной revision; Stop во время initial buffering Classic доступен |
| Progressive HEVC Main/Main10 + AAC-LC | Schema-2 fMP4 через MP4Splitter | Schema-2 fMP4 через libmpv | Тот же Qt-код | [SDK 1.14](ProgressiveHevc.md): hvc1/hev1, отдельные ресурсы, подтверждённый префикс; remux/WebM/другие кодеки отдельно |
| Progressive AV1 Main 8/10-bit + AAC-LC | Schema-2 fMP4 через MP4Splitter | Schema-2 fMP4 через libmpv | Тот же Qt-код | [SDK 1.15](ProgressiveAv1.md): av01, Sequence Header в av1C; WebM/Opus/remux отдельно |
| Native decoder shutdown и renderer lifetime | Progressive STA close-state machine; обычные/UI-owned графы синхронны | Core и software renderer — bounded worker queue; GL free у владельца | Тот же Qt-код | [SDK 1.12](ProgressiveEngineShutdown.md): Qt 16 cores / ≤2 workers, stream callbacks и Source удерживаются; [SDK 1.13](ProgressiveClassicClose.md): Classic сохраняет COM owner и окно, откладывает start/exit; detached teardown не объявляется |
| Пауза, точный seek, главы, покадровые команды | Есть | Есть | Есть | Допуски seek и ограничения live/growing-файлов |
| Скорость, повторы, A–B | Есть | Есть | Есть | Края A–B, конец очереди, пауза и число проходов |
| Действия после окончания | Есть, Win32 | Есть, Win32 | Есть, logind/session D-Bus | Один смысл постоянного и однократного действия; платформенные исполнители |
| DVD/Blu-ray/CD: заголовки/содержимое | Есть | Общий Qt-путь; физические диски не подтверждены | Реализованы в описанных границах | Отложено; не смешивать открытие ISO и интерактивное меню |
| Интерактивное меню DVD/Blu-ray | Собственные дисковые пути, поддержка зависит от вида диска | Нет полного аналога | Нет полного аналога | Отложено пользователем |
| Захват, тюнеры, аналоговое ТВ | DirectShow/BDA-пути | Нет переноса Linux-устройств на Win32 | V4L2/ALSA/Pulse/DVB частично, аналоговая настройка не реализована | Отложено пользователем |

Основание: [MainFrm.cpp](../src/apps/mplayerc/MainFrm.cpp),
[PlayerYtDlp.cpp](../src/apps/mplayerc/PlayerYtDlp.cpp),
[HarvesterSession.cpp](../src/DSUtil/HarvesterSession.cpp),
[onlineoptions.cpp](../qt/src/onlineoptions.cpp),
[progressiveapi.cpp](../qt/src/progressiveapi.cpp),
[Windows-адаптер](../qt/src/progressivesource_win.cpp), [растущий источник](../qt/src/publishedsource.cpp),
[devicesource.cpp](../qt/src/devicesource.cpp),
[deviceplayback.cpp](../qt/src/deviceplayback.cpp),
[systemactions.cpp](../qt/src/systemactions.cpp).

## Окно, меню и управление

| Возможность | Windows Classic | Windows Qt | Linux Qt | Что выравнивать |
|---|---|---|---|---|
| Объединённая нижняя панель и узкая громкость | Есть | Синхронизирована с Classic: hover/pressed, двойные подсказки, ПКМ-действия | Синхронизирована с Classic: hover/pressed, двойные подсказки, ПКМ-действия | Геометрия, таймер, состояния play/pause/mute, светлая/тёмная темы |
| Линии глав RGB 94/99/104 | Есть | Есть | Есть | Один визуальный эталон и масштабирование |
| Заголовок файла/ролика, версии, состояния | Есть | Есть | Есть | Одинаковая семантика; системный заголовок остаётся нативным |
| Верхнее и контекстное меню | Сохранены 6 штатных разделов Classic; порядок исходного главного меню скорректирован | 9 организованных разделов | Те же 9 разделов | Classic сохраняет собственные команды и локализацию; 9-раздельная Qt-схема полностью не переносилась |
| Настройки слева деревом | Есть | Есть | Есть | Общие категории и названия |
| Настройки: дерево + вкладки справа + общий поиск | Нет Qt-схемы; 26 страниц Classic | 10 категорий / 40 страниц | Та же схема | В Classic перенести только порядок, сохраняя штатные страницы и переводы |
| Окно настроек появляется с загрузкой и отменой | Нет соответствующего Qt-механизма | Есть | Есть | Qt-механизм не перенесён; Classic сохраняет штатное окно |
| Тёмная/светлая тема, логотипы, размер кнопок | Есть; все 26 страниц настроек оформлены, прокрутка проверена при захвате | Есть | Есть | Общая палитра; лиса и подпись сохраняют редакцию |
| OSD/HUD, сообщения, часы, имя файла | Есть | Есть | Есть | OSD выключено для новых профилей; включение остаётся доступным |
| Превью на таймлайне | Есть | Есть | Есть | Локальные/HTTP-границы, отмена и ресурсы |
| Горячие клавиши и жесты мыши | Есть | Есть | Есть | Общий каталог команд и проверка конфликтов пользовательских назначений |
| Базовые клавиши Classic: Ctrl+Q, Ctrl+O, Alt+X | Есть | Есть | Есть после применения classicKeys | Не считать ранние Qt-fallback значения действующими назначениями |
| Перетаскивание окна за видео, клик, двойной клик | Есть | Есть | Есть | Общие порог drag, play/pause и fullscreen |
| Медиакоманды вне активного окна | Win32 APPCOMMAND / global hotkeys | Win32 APPCOMMAND для play/stop/next/previous/seek/volume | MPRIS / playerctl | SMTC и глобальные hotkeys остаются отдельным этапом |
| WinLIRC | Есть | Нет соответствующего адаптера | Нет соответствующего адаптера | Отдельная совместимость; пульт-клавиатура использует обычные клавиши |
| Геометрия, поверх окон, полный экран, монитор | Есть | Есть, Win32/Qt | Есть, X11/Qt | Одинаковые политики; реальное оборудование не проверялось заново |

Основание: [меню Classic](../src/apps/mplayerc/mplayerc.rc),
[страницы Classic](../src/apps/mplayerc/PPageSheet.cpp),
[организованное меню Qt](../qt/src/organizedmenus.cpp),
[страницы Qt](../qt/src/classicpreferences.cpp),
[клавиши Classic](../src/apps/mplayerc/AppSettings.cpp),
[эффективные клавиши Qt](../qt/src/classicmenus.cpp),
[системные медиаклавиши Classic](../src/apps/mplayerc/mplayerc.cpp),
[MPRIS](../qt/src/mediaremote.cpp), [управление Qt](../qt/src/inputcontrol.cpp).
Количество страниц измерено по подключённым AddPage/page, а не по всем .rc-файлам;
26 и 40 не являются числом отсутствующих возможностей.

## Библиотека и пользовательские данные

| Возможность | Windows Classic | Windows Qt | Linux Qt | Что выравнивать |
|---|---|---|---|---|
| Плейлисты, вкладки, поиск, сортировки | Есть | Есть | Есть | Набор операций, текущая строка при сортировке и переходы |
| M3U/M3U8, MPCPL, PLS, ASX, CUE | Есть | Есть с оговорёнными границами | Есть с оговорёнными границами | Общий набор импорт/экспорт-образцов; DirectShow-устройства не переносимы |
| Дорожки и позиция записи, отсутствующие файлы | Есть | Есть | Есть | Дубликаты, смена вкладки, пропуск ошибок, восстановление |
| История, поиск, выборочная очистка | Есть | Есть | Есть | Позиция, названия URL, лимиты и очистка без повреждения очереди |
| Избранное и закладки с позицией | Есть | Есть | Есть | Категории, порядок, относительные пути, перенос между ОС |
| Независимый portable-профиль | Есть, свой формат | Есть, FoxicMPQt.ini | Есть, FoxicMPQt.ini | Профили остаются раздельными; бинарные/платформенные настройки не смешивать |
| Экспорт/сброс настроек | Собственный экспорт и сброс | Общий Qt-архив/копии/восстановление/сброс | То же | Общий переносимый набор с явным отображением несовместимых полей |
| Ярлык выбранного профиля | Собственные параметры запуска; не аналог Qt profile-manager | Есть, .lnk и --profile | Есть, .desktop и --profile | Общая модель выбора профиля; разный транспорт запуска |
| Импорт библиотеки и общих предпочтений между всеми тремя | Нет единого мастера | Нет Classic↔Qt-мастера | Нет Classic↔Qt-мастера | Отложено пользователем; профили остаются независимыми |

Основание: [PlayerPlaylistBar.cpp](../src/apps/mplayerc/PlayerPlaylistBar.cpp),
[HistoryDlg.cpp](../src/apps/mplayerc/HistoryDlg.cpp),
[FavoriteOrganizeDlg.cpp](../src/apps/mplayerc/FavoriteOrganizeDlg.cpp),
[playlistpanel.cpp](../qt/src/playlistpanel.cpp),
[playlistfile.cpp](../qt/src/playlistfile.cpp), [library.cpp](../qt/src/library.cpp),
[profilearchive.cpp](../qt/src/profilearchive.cpp),
[profileoptions.cpp](../qt/src/profileoptions.cpp).

## Видео, звук и субтитры

| Возможность | Windows Classic | Windows Qt | Linux Qt | Что выравнивать |
|---|---|---|---|---|
| Пропорции, crop, pan/scan, повороты, отражение | Есть | Есть | Есть | Одинаковые команды и пресеты; не путать пропорции видео с ограничением окна |
| Стереораскладки и смена глаз | Есть | Есть | Есть | Сценарии преобразования; физический 3D-экран не подтверждён |
| Выбор видеорендерера | DirectShow/EVR/внешние рендереры | Qt Software / OpenGL | Qt Software / OpenGL | Сохранять платформенные механизмы, показывать фактический вывод |
| Аппаратное декодирование | Собственные DXVA/DirectShow-настройки | auto-copy; список ручных backend ещё содержит Linux-варианты | auto-copy / VA-API / NVDEC / VDPAU | Платформенный список и проверка доступности, Windows D3D-варианты не выданы как готовые |
| HDR и 10-bit конечного вывода | Зависит от выбранного рендерера; аппаратная проверка отсутствует | Конечная Qt OpenGL-поверхность RGBA8; нет заявленного HDR passthrough | То же | Общая честная диагностика; развитие вывода — отдельный этап, не обещание HDR |
| Цвет, скейлеры, ICC | Есть, зависит от рендерера | Есть; авто ICC через Windows ICM | Есть; авто ICC X11/colord | Общие названия и результат; разные адаптеры получения профиля |
| Шейдеры | HLSL / цепочки рендерера | GLSL и ограниченный импорт mpv hook | То же | Пресеты по назначению; автоматическая конверсия произвольного HLSL не реализована |
| Внешние DirectShow-фильтры, приоритеты | Есть | Не применимо к libmpv | Не применимо к libmpv | Оставить Classic-спецификой; не рисовать неработающие галочки |
| Выбор аудиоустройства, задержка, внешние дорожки | Есть | Есть | Есть | Горячая смена, возврат к auto, исчезнувшее устройство, отсутствие тишины |
| Нормализация, gain, микшер, фильтры | Есть, собственный тракт | Есть, общий Qt/libmpv тракт | То же | Сравнивать результат и диапазоны, а не номера фильтров |
| Два аудиовыхода одновременно | Есть, два рендерера | Есть: второй WASAPI/libmpv PCM-декодер с программной синхронизацией | Есть, PulseAudio/PipeWire PCM | Сверять задержку; физические устройства и ресиверы не проверены |
| PCM/частота/bitstream | Есть, зависит от аудиорендерера | Есть настройки общего libmpv-тракта | Есть; программные замеры PCM/IEC 61937 | Реальные HDMI/S/PDIF и ресивер отложены; галочка не подтверждает тракт |
| Встроенные/внешние SRT/ASS, задержка, оформление | Есть | Есть | Есть | Общий корпус, выбор языка, шрифты, масштаб и реплики |
| Редактор/синхронизация субтитров | Есть Subresync/сохранение | Есть Qt-редактор | То же | Совместимый набор операций и сохранение форматов с оговорёнными потерями |
| Сторонние subtitle-renderer/DirectShow очереди | Есть соответствующие настройки | Нет прямого аналога libmpv | Нет прямого аналога libmpv | Остаётся спецификой Classic |

Основание: [PPageVideo.cpp](../src/apps/mplayerc/PPageVideo.cpp),
[PPageAudio.cpp](../src/apps/mplayerc/PPageAudio.cpp),
[FGManager.cpp](../src/apps/mplayerc/FGManager.cpp),
[videooutput.cpp](../qt/src/videooutput.cpp),
[videooutputoptions.cpp](../qt/src/videooutputoptions.cpp),
[systemicc.cpp](../qt/src/systemicc.cpp), [videoshaders.cpp](../qt/src/videoshaders.cpp),
[audiosourceoptions.cpp](../qt/src/audiosourceoptions.cpp),
[audioencoding.cpp](../qt/src/audioencoding.cpp),
[subtitlerender.cpp](../qt/src/subtitlerender.cpp).

## Интеграция, инструменты и выпуск

| Возможность | Windows Classic | Windows Qt | Linux Qt | Что выравнивать |
|---|---|---|---|---|
| Меню видеофайлов и проверка пути | Есть, собственная регистрация | Есть, собственная регистрация | Есть, desktop/MIME | Одни действия пользователя, разные механизмы ОС |
| Снимки, буфер, миниатюры, свойства | Есть | Есть | Есть | Одинаковые команды; область захвата и доступные форматы явно показывать |
| Сохранение копии источника | Собственные пути Save As | Qt-копия локального файла | То же | Не приравнивать локальную копию к загрузке/ремультиплексированию сетевого потока |
| Веб-пульт | Classic HTML/команды и файловый browser | Qt-пульт, LAN и слой Classic Web | То же | Общий контракт разрешённых команд; совместимость части endpoints не полная замена Classic Web |
| CLI и команды работающему процессу | Собственные ключи, HWND/slave/WM_COPYDATA | Qt CLI и instance IPC | То же, Unix-адаптер | Общие сценарии с таблицей ключей и адаптерами, сохранить старые вызовы |
| Проверка обновлений и диагностика | Свои механизмы, minidump | Qt-проверка и отчёт | То же | Общая модель канала/сборки; общего автоматического обновлятора нет |
| Автоматическая сборка в репозитории | Есть Windows workflow | Отдельного Windows Qt workflow нет | Есть Linux Qt workflow | Три сборки по общему набору изменений и общий отчёт |
| Объём автоматизированных проверок | Сборка и named-pipe capabilities проверены; полный DirectShow Harvester playback зависит от интерактивного графа | Windows Qt: progressive API 11/11 PASS, toolbar contract PASS; audio-mirror 3 PASS/2 SKIP без детерминированного WASAPI endpoint | Linux Qt: audio-mirror, progressive-api и playback-controls 3/3 PASS | Физическую задержку звука и оборудование не выдавать за программный PASS |

Основание: [integration_win.cpp](../qt/src/integration_win.cpp),
[integration.cpp](../qt/src/integration.cpp),
[webcontrol.cpp](../qt/src/webcontrol.cpp),
[WebServer.cpp](../src/apps/mplayerc/WebServer.cpp),
[launchoptions.cpp](../qt/src/launchoptions.cpp),
[CMakeLists.txt](../qt/CMakeLists.txt),
[Windows Classic CI](../.github/workflows/main.yml),
[Linux Qt CI](../.github/workflows/linux-qt.yml),
[Windows Qt build/deploy](../qt/packaging/windows/README.ru.md).

## Главные расхождения и уже общие правила

1. Windows Qt получила v1 progressive API через named pipe и второй PCM-аудиовыход; физическая синхронизация устройств ещё не измерялась.
2. В Windows Qt нет самостоятельной системной интеграции медиакоманд уровня
   Win32 Classic или Linux MPRIS. Обычные клавиши внутри окна работают.
3. Classic не получила Qt-организацию меню и настроек с общим поиском,
   а её DirectShow/renderer-возможности нельзя перенести простым копированием UI.
4. Новая Windows Qt установка получает 617 сохранённых предпочтений из шаблона,
   Linux — значения профиля и fallback функций. Например, меню в шаблоне Windows
   скрыто, а Linux-fallback показывает его. Полного общего каталога defaults нет.
5. Горячие клавиши нельзя сравнивать только по buildActions: classicKeys позже
   заменяет базовые назначения. Эффективные Ctrl+Q/Ctrl+O/Alt+X уже согласованы
   для стандартного нового Qt-профиля; сохранённый пользовательский выбор выше.
6. Общие пользовательские правила уже есть: portable-данные, OSD выключено,
   запоминание положения окна включено, ограничение пропорций окна по кадру
   выключено. `KeepAspectRatio` видео и `LimitWindowProportions` окна — разные
   параметры Classic; не обнулять первый ради совпадения со вторым.
7. Имена/логотипы редакций уже разделены. Разрыв нумерации 1.9.1.75 и 0.2.0
   отражает историю движков и сам по себе не измеряет готовность функций.

## Границы подтверждения

Для Qt 0.1.72 имеются [проверки звука и OSD](AudioOsdValidation-2026-10-24.md):
20 Windows checks и шесть затронутых Linux-групп. Это не полный совместный
прогон трёх редакций. Ранее [медиаматрица](QtLinuxMediaMatrixValidation-2026-10-19.txt)
сравнивала Linux с Classic; Classic тогда использовала null renderers, поэтому
она не доказывает совпадение изображения, реального звука и A/V.

Диски/ТВ/физические устройства отложены пользователем. Wayland пользователь
исключил из проверок. Отсутствие оборудования не обозначается как дефект,
исправленный или подтверждённый только программным тестом.
