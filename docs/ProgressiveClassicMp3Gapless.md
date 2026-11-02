# Classic: progressive MP3 gapless через обычный API

SDK 1.69, приложение 0.2.0. Продолжение [Qt-профиля](ProgressiveMp3Gapless.md)
и [проверки DirectShow-графа](ProgressiveClassicPcmGraph.md).
Windows Classic принимает `standalone-mp3-gapless-v1` через штатные `probe`,
`start`, `update`, `complete`, `status` и release API. Harvester не изменён.

## Контракт

Запрос и ограничения совпадают с Qt: одна MP3-дорожка и один ресурс,
MPEG-1 Layer III mono/stereo, 44.1/48 kHz, Xing/Info и полный encoder tag
revision 0 с правильным CRC. Обязательны SHA опубликованных ranges,
известная audible duration, VOD и `timestampOffset=0`. Прежние `mp3` и
`mp3-indexed` сохраняют encoded timeline без автоматического включения trim.

Нужно проверить capability фактического EXE:
`schema2Playback.standalone.mp3Gapless.pcmWindowAdapter` равен
`classic-directshow-mp3-window-v1` для Classic или
`qt-lavfi-absolute-samples-v1` для Qt. Остальные поля профиля одинаковы.
Клиент SDK принимает только эти проверенные adapters. В manifest сохранён
прежний общий `schema2Playback`, а дополнения редакций находятся в
`classicSchema2Playback` и `qtSchema2Playback`. Описание SDK не заменяет
capabilities установленного плеера; ZIP не обновляет EXE.

## Выбор декодера и статус

Для этого явного профиля Classic строит `CMP4SourceFilter → CMpaDecFilter`
напрямую, затем подключает обычную цепочку AudioSwitcher/renderer.
Приоритеты кодеков и внешние overrides не подменяют проверенный decoder.
Настройки обычных файлов и пользовательский INI при этом не переписываются.
PCM обрезается до пользовательской обработки, по границам `delay+529` и
`codedSamples-padding+529`. Перемотка использует прежний reader и reservoir
preroll, но задаёт новое абсолютное PCM-окно.

В `mp3Index` добавлены `pcmWindowAdapter` и `pcmWindowState`:
`idle`, `pending`, `applied` или `error`. `gaplessTrimApplied` становится
`true` только после применения окна настоящим decoder. UI/API читает
атомарную копию состояния; ему не нужна блокировка decoder.
После seek на паузе окно может быть `idle`/`pending` до поступления первого
пакета после возобновления. Stop/flush сбрасывает состояние; поздний отчёт
потока доставки после cancel не возвращает `gaplessTrimApplied=true`.

## Проверка и границы

Стенды: [обычный API](../tests/windows/progressive_family_test.py),
[Stop/cancel/reopen](../tests/progressive_classic_gapless_controls.py),
[цифровой DirectShow PCM](../tests/progressive_classic_pcm_graph.py).
Итоговые числа и exit codes находятся в
[отчёте](ProgressiveClassicMp3GaplessValidation-2026-11-02.json).

Проверка цифрового PCM не подтверждает задержку физического выхода,
многочасовой A/V drift или все пользовательские DSP-цепочки. Проверка
AudioSwitcher с фиксированным gain относится к этой конкретной обработке.
MPEG-2/2.5, другие rates/layouts, VBRI gapless, live и остальные пункты
[общего чеклиста](ProgressiveCompletionChecklist.md) остаются открытыми.
