# Проверка фоновой публикации — 26 октября 2026

[Контракт SDK 1.8](ProgressivePublication.md). Базовый commit
`e5b4bb2230a83ece55870b34d1dc3e28e3dca3d7`. [Машинный отчёт с хешами](ProgressivePublicationValidation-2026-10-26.json).
YouTubeHarvester не изменялся. Windows установленные EXE/профили не заменялись.

## Результаты

| Редакция | Проверки | Итоговый exit |
|---|---|---:|
| Linux Qt | 5 baseline CTest-групп; финальный полный API: 45 QtTest результатов; 14 publication сценариев (16 с init/cleanup) | 0 |
| Windows Qt | 6 baseline CTest-групп; финальный полный API: 46; финальные preparation 13 и publication 16 | 0 |
| Classic | SDK/reader/resource/v2 playback regression; финальные update+Close / complete+Stop и отдельный legacy/provider playback | 0 |
| Native media bridge | 17 случаев на GCC/MinGW/MSVC, те же 17 ASan/UBSan с leak detection | 0 |
| Python SDK / семейный контракт | 32 / 2 теста | 0 |

Qt UI/audio — частные X11/D-Bus/PulseAudio; Windows Qt — отдельные профили.
Новые `publishedUpdatesKeepUiResponsive` используют настоящий IPC и управляемое
ожидание внутри публикации источника, после валидации v1 и перед файловым I/O.

Проверены fMP4 v1, indexed MP4 v1 и fMP4 v2, update и complete:

- Пока worker занят, UI продолжает события; status/capabilities/heartbeat отвечают.
- Availability остаётся прежней, v2 revision не меняется до завершения индексации.
- Конкурирующая публикация возвращает preparation_busy; старая revision не меняет источник.
- Готовое обновление увеличивает доступную границу, complete публикует конечное состояние.
- Close не ждёт gated worker (на стенде <200 мс) и запрещает поздний запуск.
- В двух v2-сценариях disconnect после входа в публикацию не откатывает допустимый
  update/complete; status показывает точную применённую revision.

V2 media writer на обеих Qt-системах и Classic дошёл до позиции >=10 секунд,
пока оба физических файла ещё недокачаны, затем rename+append/complete, natural
finished и files-closed receipt. Generation UINT64_MAX и revision выше 2^53 сохранены.
Legacy Qt: complete/failed; Windows дополнительно provider restart.

## Реальное окно Classic

Тестовый ролик длится **20 секунд**, изначально доступны только **5 секунд**. В следующий опубликованный
префикс видео добавлены 100000 корректных free atoms, требующих настоящей доиндексации.
Перед UI-pings тест проверяет GetProcessIoCounters: минимум 500 новых чтений,
worker ещё не завершился. Затем WM_NULL и Close для update / Stop для complete.

Все шесть UI-pings получили ответ, максимальное ожидание **2.6866 мс** на стенде.
Обе операции возвращают preparation_cancelled, status не содержит активного
источника или ошибки повреждения MP4; readerRelease подтверждает filesClosed,
после чего readerForget удаляет квитанцию. В JSON поле readOperationsBeforeCancel
взято из прежнего имени метрики: фактически это общий счётчик процесса до проверки
release, включая служебный I/O. Доказательство перекрытия с UI — отдельная проверка
>=500 чтений до отправки пингов; атрибуция всех чтений одному потоку не заявляется.

Отдельно проверен сохранённый CLI `/harvester-session` с offline provider:
playing до complete, position >=10 секунд при недокачанных файлах, обновлённый
readyEnd >=19 секунд после complete, Close и освобождение provider lease.
Полный Classic regression предшествовал последней корректировке обработки отмены
как нормального Stop; после неё повторены реальные publication/Stop и legacy тесты.
SHA-256 финальной тестовой копии совпал с собранным EXE.

Classic INI заранее содержал ContextMenuPrompted=1 и ContextMenuEnabled=0,
страница Formats не открывалась. HKCU команды/иконки установки совпали до/после,
процессы завершились с 0, одноразовая задача удалена, LastTaskResult=0.

## Промежуточные ошибки и повторная проверка

Первый Classic-прогон завершился с 1: при MLS_LOADING команда Stop меняла
намерение API, но не закрывала активный progressive-сеанс. Stop теперь отменяет
источник и в этом состоянии; внутреннее закрытие графа не трогает UI-owned API
через обработчик Stop. Повторные проверки прошли.

Один Linux-прогон завершился с CTest exit 8 (44 PASS / 1 FAIL): старый тест
ожидал >=5 срабатываний таймера строго за 100 мс. Под нагрузкой VM события
объединялись. Проверка заменена ожиданием продвижения UI с пределом 1 секунды,
при всё ещё заблокированном worker. Полный API повторён: 45 PASS, exit 0;
Windows preparation/publication с тем же исправленным harness также прошли.
Это изменение тестового ограничения, не изменение таймеров установленного плеера.

## Установка и ограничения

Проверенный Linux EXE заменён атомарно, прежний сохранён для отката.
SHA-256: `22948a3c1df459ad1a65c2ec5e3081e274b4b0c54482d52aa04f0d52a0d90500`.
INI до/после: `ac9387b8d09f63317ed8ea3b681abad96e5abe8890400d4db6540017e3acde75`.
Профиль/личные файлы и текущий просмотр не менялись. Проверены --version (0.2.0)
и capabilities установленного EXE: offUi содержит четыре команды, updatesOffUi=true.

Проверены программные состояния, чтение, позиция медиадвижка и закрытие файлов.
Физический A/V sync, оборудование, Wayland и реальный Harvester не проверялись.
Весь пункт ТЗ про неблокирующий I/O ещё не завершён: остаются registry I/O на UI,
legacy/provider opening и часть финального закрытия. Принудительная отмена
любого native read и универсальное закрытие за 2 секунды не обещаются.
Копии индексов Classic расходуют дополнительную память; предельный ресурсный
тест на 2 млн samples ещё впереди. Новые codec/live/remux/metadata не добавлены.
SDK ZIP не содержит EXE и не обновляет установщики 0.2.0.
