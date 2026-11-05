# Continuous remux: внутренний компонент B-frames

5 ноября 2026. Приложение 0.2.0, SDK 1.82. Это расширение частного протокола
FFmpeg worker, **не публичная поддержка B-frames при continuous playback**.
[Первичные результаты, SHA и exit codes](ProgressiveContinuousReorderValidation-2026-11-05.json).

## Изменение

`tools/progressive_stream_remux_worker.cpp` принимает дополнительный внутренний
режим `continuous-h264-reordered-video-v1`: один H.264 video-only track обычного
MP4, start_time=0, известная длина, прежние ограничения I/O, размера и пакетов.
`video_delay` допускается от 0 до 16. Звук по-прежнему отклоняется до записи
output header. Публичный parent/controller/SDK новый mode не отправляют;
`--info`, capability, CLI/API и SDK остаются с прежним профилем без B-frames.

Простого удаления запрета reorder недостаточно. Контрольный эксперимент с
прежними `empty_moov+negative_cts_offsets` сохраняет payload, но переносит первый
PTS с 0 на 40 ms, DTS с −80 ms на 0. Это **контрфактическая частная сборка**
с разрешённым новым mode, а не обнаруженный отказ ранее поддержанного профиля.
В старом публичном профиле такие источники всегда отклонялись.

Новый mode использует `delay_moov+frag_keyframe+default_base_moof`, явную edit
list и movie timescale, равную знаменателю исходной timebase. Заголовок выходит
при первом fragment, когда известен decode delay. Начальные отрицательные DTS
и presentation timeline сохраняются без общего сдвига и округления к 1 ms.
Прежняя ветка mux flags для `continuous-h264-video-v1` сохранена.

## Проверки

Четыре 24-секундных профиля с реально немонотонными PTS в порядке декодирования:

| Профиль encoder | Частота | Packets / decoded frames |
|---|---:|---:|
| H.264 `-bf 2` | 25 | 600 |
| H.264 `-bf 3` | 30000/1001 | 720 |
| H.264 `-bf 16` | 60000/1001 | 1439 |
| H.264 `-bf 2`, VFR | переменная | 480 |

`-bf` — настройка encoder, не утверждение о длине каждой фактической B-серии.
Стенд проверяет наличие reorder; VFR дополнительно имеет несколько интервалов
между presentation timestamps. Кодеки генерируются FFmpeg соответствующей ОС;
bitstream между ОС не обязан совпадать.

Для каждого профиля parent-стенд отдаёт только текущий префикс входных bytes,
увеличивает его четырьмя публикациями внутри packet и удерживает тот же worker.
Первые read replies короткие. Полный input и объявленная доступность различаются;
это **worker AVIO harness**, не запись настоящего partial через ResourceSource.

Четыре промежуточных output prefix каждого профиля обрезаются строго по полному
moof/mdat. Их packet payload SHA и рациональные PTS/DTS/duration **точно** равны
соответствующему префиксу оригинала. Независимый FFmpeg со строгими decode errors
проверяет framemd5 каждого доступного кадра; это не только проверка контейнера.
Полный output даёт такое же точное совпадение всех пакетов и кадров.
В сумме на каждую сборку — 3239 полных кадров и 16 промежуточных проверок.

Дополнительно: cancel при input wait и output ACK, unconfirmed EOF, integrity
error, kill при wait; старый mode отклоняет B-frames, новый отклоняет audio.
Cancel/error не считаются complete. Проверены source SHA, output byte count/SHA,
один worker/context через ожидания и удаление временного рабочего каталога.

| Сборка | Новые сценарии | Прежние worker-сценарии | Exit драйверов |
|---|---:|---:|---:|
| Linux GCC | 11 | 9 | 0 / 0 |
| Linux ASan/UBSan | 11 | 9 | 0 / 0 |
| Windows MinGW | 11 | 9 | 0 / 0 |
| Windows MSVC | 11 | 9 | 0 / 0 |

Отдельно прежние **пять SDK playback-сценариев каждой редакции** прошли на
реальных player endpoints с новым MinGW/GCC worker в частных копиях. Проверены
подготовка без Play, lost replies, pause, complete/seek/EOF, Stop, независимый
endpoint, filesClosed/forget и прежние отказы audio/B-frames. Это проверка
совместимости, не воспроизведение нового mode через плеер.

Исходный worker штатно отклонил новый mode, exit теста 1. Контрфактическая сборка
со старыми mux flags провалила точное сравнение clocks, exit теста 1; отдельный
замер сохранил сами значения сдвига. Первый Windows запуск остановился в
эталонном FFmpeg: удалённый в этой версии `-vsync`. Стенд переведён на
`-fps_mode passthrough`; исходные ошибки и их SHA сохранены. Финальный неизменный
исходник нового теста выполнен на всех четырёх сборках.

## Повторение и границы

CTest: `progressive-stream-remux-reorder`, timeout 300 s; регистрация проверена
на обеих ОС. Для отдельного запуска:

```sh
python3 tests/progressive_stream_reorder_test.py \
  --worker /absolute/path/to/private-worker \
  --report-directory /absolute/path/to/new-report-directory
```

Нужны FFmpeg/ffprobe; report directory должен быть новым. Native-проверки
не используют рабочий стол/звуковой сеанс. Linux SDK playback выполнялся только
через `qt/tests/run_tests.sh` с частными X11/D-Bus/PulseAudio. Classic запущен
из двух копий с ContextMenuPrompted=1/ContextMenuEnabled=0 до первого запуска;
страницы настроек не открывались. HKCU shell до/после совпал; одноразовая задача
удалена. 17 защищённых файлов, установленные EXE/worker/INI и SDK ZIP сохранены.
Harvester и настройки Windows не менялись.

Новый worker собран только в частных каталогах. Пользовательской версии или
нового main EXE нет; установленный Linux остаётся проверенным `ebb0a806…`.

Следующие шаги: admission нового mode в managed parent, output cache/index с
edit list и reorder, actual libmpv/DirectShow playback/seek/EOF, затем публичный
contract и поставка. Audio/AAC trim, прочие кодеки, HD/4K, многочасовой stress,
физические A/V измерения и прежние unexplained latency/resource outliers
этой проверкой не закрыты. Wayland и отложенное оборудование не проверялись.
