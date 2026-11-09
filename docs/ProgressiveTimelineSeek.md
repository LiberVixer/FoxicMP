# Ожидающий переход на таймлайне

9 ноября 2026. Продолжение [очереди SDK 1.87](ProgressiveQueuedSeek.md).
CLI/API/SDK остаются совместимыми; новые поля протокола не добавляются.
Harvester не меняется.

В continuous remux playback щелчок или перетаскивание в ещё не подтверждённую
часть шкалы выбирает одну ожидающую цель. Новый выбор заменяет прежнюю.
При подтверждении PTS-prefix приложение применяет guarded переход. До этого
текущее воспроизведение и ручная пауза сохраняются. Положение жёлтой метки —
цель; основной ползунок показывает фактическую позицию. В Qt dragging рисует
предварительную цель, а очередь создаётся при отпускании ЛКМ. Classic сохраняет
свой штатный drag-on-move, заменяя одну цель по мере движения.

ПКМ в пределах метки отменяет только ожидающую цель. Нажатие правой кнопки
в других местах сохраняет прежнее поведение; Classic сохраняет переключение
оставшегося времени. Подсказка шкалы содержит `… → время` и подсказку отмены.
Обычный принятый seek, Stop/Cancel, ошибка и замена источника убирают метку.
Уже переданный декодеру переход отменой не разворачивается назад.

Цвет метки Qt/Classic dark — RGB 214/165/72; Classic light — 146/100/20.
Метка — тонкая вертикальная линия с короткой верхней отметкой, отличимая от
линий глав RGB 94/99/104 и фактической позиции. Остальной стиль Classic,
страницы настроек, defaults и сохранённые профили не меняются.

Действуют прежние ограничения: узкие continuous MP4/H.264 и H.264/AAC-LC
профили, одна ожидающая цель, 16 применённых growing переходов до complete.
Выбор не заставляет загрузчик скачивать диапазон; очередь живёт при действующем
writer heartbeat. Физическое наличие байтов не заменяет подтверждение writer.
После исчерпания квоты Qt-шкала growing seek блокируется; Classic rejected
переход не меняет decoder и не выдаёт его за выполненный.

Classic также поддерживает WM_PRINTCLIENT/WM_PRINT(PRF_CLIENT) общей функцией
рисования seekbar для штатного offscreen-захвата. Контракт Win32:
[WM_PRINTCLIENT](https://learn.microsoft.com/en-us/windows/win32/gdi/wm-printclient).
Этот захват сам по себе не проверяет desktop renderer видео или физический A/V.

Проверки Qt включают настоящие mouse press/move/release, проверку цвета метки,
сохранение фактической позиции во время drag, superseding, ПКМ и очистку после
publish/Stop/replacement/error. После publish сохраняются pause/speed/title.
Classic проверяется сообщениями реальному seekbar в частном интерактивном
окне обеих тем, с GDI-снимками до выбора, при ожидании и после отмены.
Hash шкалы до выбора и после отмены должен совпадать; это проверяет сохранение
прежней отрисовки, а не только отсутствие жёлтой метки.

Первоначальные build/capture/fixture/drag отказы сохраняются в
[отчёте](https://github.com/LiberVixer/FoxicMP/blob/main/docs/ProgressiveTimelineSeekValidation-2026-11-09.json).
Чёрный захват окна SSH-сеанса не считается успешной визуальной проверкой.
Одноразовая задача интерактивного теста удаляется после прогона; периодические
задачи не создаются. Main Classic использует private Null Video Renderer
(Uncompressed), что не проверяет desktop video renderer или физический A/V.

Реальные снимки шкал из тестов:

| Linux Qt | Windows Qt |
| --- | --- |
| ![Linux Qt](https://raw.githubusercontent.com/LiberVixer/FoxicMP/main/docs/images/progressive-timeline-linux-qt.png) | ![Windows Qt](https://raw.githubusercontent.com/LiberVixer/FoxicMP/main/docs/images/progressive-timeline-windows-qt.png) |

| Classic dark | Classic light |
| --- | --- |
| ![Classic dark](https://raw.githubusercontent.com/LiberVixer/FoxicMP/main/docs/images/progressive-timeline-classic-dark.png) | ![Classic light](https://raw.githubusercontent.com/LiberVixer/FoxicMP/main/docs/images/progressive-timeline-classic-light.png) |

Это контрольные изображения самой шкалы; длительности и ширины тестовых окон
различаются. Видео этих private/null/software renderer стендов не используется
для заявления физической синхронизации.

## Конечная проверка и установка

Обычные main EXE: 222 сценария, по 74 на редакцию; отдельно полный Qt
Window/Player suite обеих ОС — по 20 cases, и четыре интерактивных сценария
Classic на каждую тему. Всего до установки 270. Конечные test drivers exit 0.
Повтор Linux main сохранён отдельно: в первом прогоне driver receipt не
сохранился, его успешный текст CTest не подменяет неизвестный exit code.

В светлой теме выявлена нестабильная строка при offscreen capture: print DC
требовал явной client clip перед штатными RGN_DIFF/RGN_XOR. Исправлен только
маршрут print, сравнение полного RGB-хеша до выбора и после отмены осталось
строгим. Сохранён и исходный Classic same-X drag failure, после исправления
все восемь интерактивных cases проходят. До overlay проверяется отсутствие
пикселей метки, при ожидании — их наличие, после отмены/терминального события —
отсутствие. Чёрные SSH captures и первоначальная ошибка fixture не засчитаны.

Windows Qt после 20 PASS записал `QEventDispatcherWin32::wakeUp: Failed to post
a message`, native exit 0; предупреждение сохранено. Полная shutdown/latency/
ресурсная/многочасовая приёмка остаётся открытой. Инъекции аварий принадлежащего
тесту worker и принудительная очистка idle peers имеют собственные process exits,
не объявляются штатными exits 0.

Установленный Linux EXE обновлён атомарно с backup
`_bin/FoxicMPQt-linux-backup-before-timeline-sdk187-20261109T113501Z`.
Worker и Python-модули не менялись; управляемая документация/manifest SDK 1.87
обновлена. Дополнительные 23 установленных cases прошли с driver exit 0;
всего в этом срезе 293 успешных cases/checks. INI/личные файлы сохранены.
Из 586 защищённых файлов 579 совпали, 7 — ожидаемые обновления Linux EXE/SDK;
новый документ добавлен отдельно. 158 HKCU shell entries совпали, Windows
установки не заменены. Одноразовая интерактивная задача завершилась с 0 и
удалена; периодических задач нет. SDK ZIP проверен по 326 файлам/10 imports;
нового public binary release нет.
