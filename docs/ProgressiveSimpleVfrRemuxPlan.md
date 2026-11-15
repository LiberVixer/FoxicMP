# R01: SimpleBlock VFR, подготовка следующего профиля

15 ноября 2026. Основание — завершённый [SDK1.101 BlockDuration VFR](ProgressiveContinuousWebmOpusVfr.md).
Этот этап добавляет **только тестовый потоковый прототип и проверки**.
Приложение остаётся 0.2.0, SDK — 1.101. Нового public режима/capability нет.
Harvester и установленные приложения/профили не меняются.

## Что проверено

[Сводный отчёт](ProgressiveSimpleVfrPreflight-2026-11-15.json) содержит отдельные
результаты Linux Qt, Windows Qt и Classic: 18 строк (по шесть на редакцию),
54 варианта дробного чтения, 18 проверок lookahead, 33 отрицательных контроля.
Fixtures длительностью 24 секунды: VP9 p0/8-bit и p2/10-bit, переменные интервалы
от базовых 50/60000÷1001 fps, Opus mono/stereo, finite/unknown Clusters.
Video DefaultDuration удалён; входное видео остаётся в SimpleBlock.

`tests/progressive_simple_vfr_prototype.py` получает только очередные подтверждённые
bytes, без карты полного файла. Он удерживает один видеопакет до следующего PTS,
перепаковывает его в BlockGroup с BlockDuration и выпускает завершённый Cluster.
Lookahead проверен отдельно: предыдущий Cluster выходит после тела следующего
видеопакета, до конца следующего Cluster. Последний кадр ждёт подтверждённый EOF
и известный endpoint. Размер существующего файла сам по себе не означает EOF.

Входные SeekHead, Cues, Position/PrevSize и CRC намеренно добавлены в fixtures;
исходные CRC проверены до конвертации. После изменения структуры устаревшие
указатели и CRC удалены. Fake Cluster-ID внутри Void не создаёт границ.
Packet hashes, PTS/DTS, decoded frameMD5 и float PCM до/после совпадают.
18 canonical outputs приняты существующим VFR worker с публикацией фрагмента
до source-complete; 18 PCM-прогонов прошли настоящие адаптеры Qt/Classic.
Qt совпадает точно; максимальная ошибка Classic — менее 4e-8 float.
Это не измерение физического A/V выхода.

Прямой SimpleBlock вход во всех 18 строках штатно отклонён существующим режимом
с `unsupported-stream-webm-video-duration`. **Ordinary main с новым SimpleBlock
режимом не проверялся: такого production режима пока нет.**

## Почему нужен отдельный контракт

При отсутствии BlockDuration и DefaultDuration длительность выводится из
следующего блока в порядке отображения; последнему кадру нужен внешний endpoint.
Это следует из [Matroska BlockDuration](https://www.matroska.org/technical/elements.html#BlockDuration).
Первый профиль ограничивается PTS=DTS, поэтому не подменяет display order
порядком декодирования при reorder. ReferenceBlock=0 добавляется только для
неключевых кадров этого ограниченного профиля.

Перепаковка меняет смещения и данные родительских элементов. Сохранить прежние
SeekHead/Cues/CRC нельзя. CRC — необязательный элемент, покрывающий данные
родителя без самого CRC; см. [RFC8794 §11.3.1](https://www.rfc-editor.org/rfc/rfc8794.html#section-11.3.1).
Удаление устаревшего CRC в canonical output не означает полной проверки CRC
произвольного входного WebM в прототипе.

## Следующий production этап

1. Зафиксировать отдельный mode, предварительное имя
   `continuous-webm-vp9-opus-simple-vfr-v1` (ещё не API). Старый SDK1.101 mode
   с обязательным BlockDuration сохраняет свой admission без расширения.
   Геометрия VP9/Opus наследуется из bounded профиля; video DefaultDuration
   отсутствует, PTS=DTS начинается с нуля, frame/audio counts и endpoint известны.
2. Перенести incremental state machine в общий C++ scanner. Он читает только
   подтверждённые source ranges, сохраняет состояние на partial header/body,
   держит один unresolved video packet и выпускает предыдущий Cluster сразу
   после next-PTS. Последний BlockDuration появляется только после writer EOF.
3. Ввести явные bounds: input ≤128 MiB, head ≤1 MiB, Cluster ≤16 MiB,
   pending ≤32 MiB/32 Clusters, frame count ≤50000, endpoint ≤1 часа,
   interval 1–1000 ms; отдельно ограничить output/cache ≤64 MiB. Перед публикацией
   применять существующие VP9/Opus validators. Не ограничиваться codec IDs.
4. Согласовать original input length с **динамическим** размером canonical output.
   Не переносить fixture oracle: production не знает финальный размер результата.
   Частичный output не объявлять complete; cancel/release удаляют только owned cache.
5. Удалять либо перестраивать устаревшие metadata/CRC, сохраняя codec private,
   CodecDelay, audio BlockGroups/DiscardPadding и непрерывность clocks.
   Явно определить допустимые Tags: прототип принимает их только до media и
   отклоняет после первого Cluster; это ещё не общий Matroska remux.
6. Провести mode identity через worker/cache/SDK и Qt/Classic decoder adapters,
   включая повторную Classic индексацию. Сохранять exact counts/endpoint и
   совместимость прежних режимов; не трактовать новый mode как старый CFR.
7. До объявления capability проверить обычные main всех трёх редакций:
   ≥10 секунд до complete, pause/seek/queued target/release, exact PCM,
   partial structures/ошибки clocks/EOF, memory/cache bounds и старые режимы.
   После новой проверенной сборки атомарно обновить Linux с backup/неизменным INI.

## Воспроизведение подготовки и ограничения

CTest `progressive-simple-vfr-preflight` зарегистрирован в `qt/CMakeLists.txt`.
Linux запускать через `qt/tests/run_tests.sh` с частными сервисами. Прямой запуск:

```text
python3 tests/progressive_simple_vfr_preflight.py --worker <worker> --runner <PCM-probe> --edition qt --report-directory <private-report>
```

Для Classic используется его отдельный PCM probe, `--edition classic` и отдельный
INI с `ContextMenuPrompted=1`, `ContextMenuEnabled=0`; HKCU проверяется до/после.
Нужны FFmpeg/ffprobe с VP9 p0/p2 и Opus, существующие helpers из `tests/`, SDK1.101
worker и соответствующий PCM probe. Сам прототип не является SDK-модулем.

Parser bounded по входному и pending буферам, но не имеет собственного лимита
общего output: его дренирует caller, далее действует существующий cache budget.
Полная проверка codec bitstream делегирована существующему C++ worker.
Тестовый consumer использует уже полученный canonical файл и известный финальный
output length; это oracle для проверки конвертации, **не production integration**.
Ни prototype, ни PCM probe не доказывают прямое progressive-воспроизведение
SimpleBlock в обычном окне или физическую синхронизацию звука и изображения.

[Фиксированный чеклист этого этапа](ProgressiveSimpleVfrExecutionChecklist.md)
отделён от 77 широких открытых требований ТЗ. Подготовка не закрывает R01/R04.
