# Проверка optional SRT, 29 октября 2026

SDK 1.24: **PASS** в Classic Windows, Windows Qt и Linux Qt. Изменён общий
медиамост и обработка отказа загрузки SRT в обоих backend. YouTubeHarvester,
установленные Windows EXE/INI и профили MPC/MPMY не изменялись.

| Редакция | SRT и контроль обязательного A/V | Дополнительная проверка |
|---|---:|---|
| Linux Qt | 8 / 8 | Меняющиеся декодированные кадры, AAC 48 kHz, текст исправной SRT |
| Windows Qt | 8 / 8 | Те же проверки на Windows, отдельные профили |
| Windows Classic | 8 / 8 | Native загрузка SRT, счётчики чтения, время графа, естественный EOF |

Сценарии: неверный исходный UTF-8 SRT; повреждённый поздний SRT; усечение и
перезапись оригинала после создания проверенной копии; отсутствие завершённого
файла; failed optional writer; удалённая приватная копия перед подключением;
контрольное усечение обязательного аудио. Семь optional-ошибок сохраняют A/V;
контроль обязательного аудио даёт error и освобождает читателя.

Исправная SRT подключается после отказа другой. При повреждении оригинала
приватная копия сохраняет прежний SHA-256 и продолжает использоваться. Ошибка
backend не мешает подключить следующую SRT или перемотать назад; Qt проверяет
также текст выбранной дорожки после переоткрытия. До complete проиграно не менее
10 секунд, хвосты A/V физически отсутствуют. После append/rename/complete — natural
EOF и released. Session/generation/readerId не заменяются. Classic использует
штатный относительный переход на 2 секунды; Qt — абсолютную цель 2 секунды.

## Регрессия

В каждой Qt-сборке пройдены API/reader/lifetime сценарии и 10 multi-audio seek
профилей SDK 1.23, включая cached/paused/tail transitions. На Linux дополнительно
19 legacy/source-тестов. Маршрутизация декодеров: H.264 fMP4, VP9 WebM и mixed
H.264/Vorbis. Это не новая полная проверка прежних 50 codec-профилей.

Общий native media-корпус: 459 проверок на Linux и Windows. Дополнительный
ResourceSource корпус: 30 на Linux, 28 на Windows; default strict, opt-in optional,
ссылки из required/init/segments/segment-init проверены отдельно. Python SDK/family:
29 тестов. Classic также прошёл multi-audio/late SRT, mixed H.264/Vorbis и legacy
provider. Все итоговые exit codes — 0. Точные хеши трёх EXE, QTest totals и снимки
состояний — в [JSON отчёте](ProgressiveOptionalSrtValidation-2026-10-29.json).

Linux UI/audio выполнялись через private X11/D-Bus/PulseAudio. Windows Qt —
offscreen, mute в отдельном профиле; Classic — отдельный EXE/INI с
ContextMenuPrompted=1 и ContextMenuEnabled=0. Контекстное меню/иконки HKCU
сверены до/после; изменений нет. Одноразовая интерактивная задача и её временные
каталоги удалены. Linux EXE заменён атомарно, прежний сохранён, INI/личные файлы
проверены на неизменность.

## Исправления стенда и границы

Контрольный required audio правильно закрывает сеанс: тест исправлен на чтение
status после закрытия вместо недопустимого heartbeat. Classic-команда шага назад
относительная, поэтому проверка фиксирует реальное уменьшение позиции.

Не проверены сквозной вызов из Harvester, физический звук/дрейф, большие файлы,
оборудование и Wayland. Optional audio, главы/metadata, live/segments и muxed/remux
остаются отдельными этапами. Ready/state описывают проверенную копию; loaded и
backendError описывают результат её подключения в активном backend.
