# Classic: PCM-окно в настоящем DirectShow-графе

Продолжение [внутреннего decoder adapter](ProgressiveClassicPcmWindow.md).
Проверяется production-цепочка `CHarvesterFileReader → CMP4SourceFilter →
CMpaDecFilter → PCM capture renderer`, без физического аудиовыхода.
[Отчёт от 2 ноября 2026](ProgressiveClassicPcmGraphValidation-2026-11-02.json).

Это внутренний opt-in графа. Публичные API и capabilities Classic сохраняют
отказ на `mp3-gapless`: приложение ещё должно гарантировать выбор внутреннего
декодера, отражать его фактическое состояние и пройти приёмку управления/UI.
SDK остаётся 1.68, приложение — 0.2.0. Harvester не изменяется.

## Передача плана

`CHarvesterSession::OpenPublished` получил необязательный C++ opt-in,
выключенный по умолчанию. Он используется стендом; JSON-запрос или настройка
профиля сами не включают этот путь. Метаданные, CRC, duration, SHA и immutable
frames проверяет прежний shared bridge. Сессия сохраняет проверенные begin/end
и sample rate из trim plan; время index/chapters остаётся audible.

MP4-сплиттер при начале каждого сегмента выбирает frame-aligned preroll через
свой индекс. Номер первого декодируемого sample берётся из номера MP3 frame
(`frameIndex*1152`), а не из округлённого PTS. Seek target переводится из целых
100 ns к ближайшему sample и ограничивается последним полезным sample.

Новый частный COM-интерфейс `IProgressivePcmControl` не меняет старый
`IMpaDecFilter`. Version 1 передаёт границы и decodeFirst, а диагностические
flags различают pending/applied/error. Неизвестная версия, неверные границы
и повторное вооружение после начала PCM отклоняются. Декодер без совместимого
интерфейса даёт ошибку до вывода звука.

Порядок важен: output thread MP4-пина выполняет собственный startup flush,
а первый compressed packet вызывает ещё один flush декодера по discontinuity.
Поэтому план хранится в MP4 output pin и передаётся непосредственно перед
доставкой первого пакета, после startup flush. Декодер применяет pending plan
после обработки discontinuity первого пакета. Раннее вооружение из demux thread
не используется: реальный тест обнаружил, что startup flush стирал его.

Seek/flush/NewSegment очищают прежний план, следующий сегмент вооружается
заново. Stop и разрыв соединения также очищают его. Неожиданная discontinuity
или изменение input media type после применения окна завершают этот путь
ошибкой, не переводя его на необрезанный звук. Поведение обычных графов без
PCM-плана сохраняется.

## Проверки

24 checks, четыре libmp3lame fixtures: mono/stereo × 44,1/48 kHz.
Для каждого профиля снимаются четыре PCM-окна:

- Начало: двухсекундный confirmed prefix играет до complete; затем весь prefix
  без complete продолжает играть, но не даёт EOF. Writer complete открывает хвост.
- На `2*rate+1` выполняются pause/seek/run ещё до writer complete, в том же
  graph instance и с прежним reader. Возобновлённый PCM проверяется до complete;
  EOF всё ещё ждёт writer. Sample 1 и последний sample дополнительно проверены
  через перемотку того же графа после завершения источника.
- На выходе реального декодера, после преобразования формата в interleaved
  float, проверяются точная длина и побайтовое равенство соответствующему
  участку полного PCM того же Classic engine.
- Capture проверяет начало каждого блока в пределах трёх 100 ns ticks от
  точного sample clock, монотонность и выравнивание PCM.
- Подтверждаются фактически applied window, очистка после Stop и реальное
  закрытие файлов/release после разрушения графа, до выхода процесса.
- Несовместимый downstream без PCM-control отвергается с EC_ERRORABORT,
  нулём выведенных samples и release. Исходники не изменяются.

Стенд использует отдельный portable INI с отключённым shell prompt. Нулевой
reference clock и capture renderer не обращаются к микшеру или устройству.
В полном Classic отдельно повторены прежние indexed MP3, plain MP3, ADTS и
raw FLAC, а также публичный отказ новому профилю без открытия reader.

Для повторения сначала собрать Classic и его зависимости в x64 MSVC Developer
Command Prompt, затем выполнить `tests\windows\build_classic_pcm_probe.cmd`
и `tests\windows\build_classic_pcm_graph_probe.cmd`:

```
python tests/progressive_classic_pcm_graph.py --runner _bin/classic-pcm-graph-probe/probe.exe --decoder-runner _bin/classic-pcm-probe/probe.exe
```

Нужны Python 3.10+, FFmpeg/FFprobe и libmp3lame в тестовом PATH.

## Следующая граница

Остаются принудительный выбор внутреннего decoder в приложении для этого
профиля, playback status и отображение ошибки, UI pause/seek/reopen/cancel,
пользовательская AudioSwitcher-обработка и другие скорости. Только после их
проверки можно включить Classic capability и обновить SDK. Эта проверка не
подтверждает физический выход, A/V drift, другие MPEG profiles, длительный
stress-тест или интеграцию настоящего Harvester. Qt и установленная Linux
сборка SDK 1.68 этим Windows-этапом не меняются.
