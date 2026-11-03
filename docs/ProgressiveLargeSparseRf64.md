# Физические sparse RF64 больше 4 GiB

Проверка SDK 1.81 на прежних production binaries: RF64 PCM16 и PCM24,
stereo 48 kHz, явные `rf64-sparse-pcm16` / `rf64-sparse-pcm24`.
Результаты трёх редакций собраны в [отчёте от 3 ноября 2026](ProgressiveLargeSparseRf64Validation-2026-11-03.json).
Это дополнительная проверка существующего контракта. Production sources и
EXE совпадают с проверенными в SDK 1.81; пересобран только Qt test harness.
Новый fixture доступен в исходниках текущей ветки; опубликованный ZIP и tag
SDK 1.81 остаются неизменными. Установленный Linux EXE уже соответствует
этой версии, поэтому повторная замена не нужна.

Тест создаёт файл 4 GiB + 16 MiB + header, сначала записывая лишь начало,
16-секундный дальний островок и десятисекундный хвост. Незаписанные области
проверяются через SEEK_DATA в Linux и FSCTL_QUERY_ALLOCATED_RANGES в Windows.
На Windows FSCTL_SET_SPARSE применяется только к временному файлу теста.
Размер файла не считается доказательством готовности данных.

Через публичный API проверяются head startup, ручная пауза, отказ перехода
в дыру, paused seek в островок за 2^32, фактическое воспроизведение там,
ожидание конца островка, его расширение и обратный переход к началу.
При дальнем playback диагностический physicalReadMaxEndBytes превышает 2^32,
а decoderIoBytes.audio остаётся меньше 64 MiB: последовательное чтение
всех четырёх гигабайт не выдаётся за индексный seek. Qt дополнительно
наблюдает реальные time-pos, audio codec, sample rate и channel count libmpv.
Classic проходит отдельный интерактивный Windows-прогон с частным INI.

Ложный complete при оставшихся дырах должен быть отклонён без замены reader,
завершения сессии и изменения принятой revision. Дыры остаются физически
незаписанными во время дальнего playback. Независимый FFmpeg декодирует
полсекунды островка и сверяет PCM с формулой writer (период 4093 frames).

Затем writer заполняет только отсутствующие области порциями не больше
16 MiB новых bytes. Уже опубликованные байты не перезаписываются, ручная
пауза и reader сохраняются. После полной записи всех bytes независимый SHA-256
файла сравнивается с SHA формулы. Только затем writer сообщает complete;
проверяются paused seek к хвосту, естественный EOF, release и удаление файла.
Запросы API остаются в лимите 64 KiB; используются внешние digest indices.

Границы результата: два stereo 48 kHz профиля; максимум 16 GiB / 250000 packets,
другие rates/layouts, долгий stress, video, другие контейнеры и live этим
прогоном не подтверждены. Полный чеклист ТЗ остаётся открытым.

Воспроизводимый сценарий: `tests/progressive_rf64_sparse_large_playback.py`;
CTest `progressive-rf64-sparse-large-playback` запускает оба профиля.
Linux запускается с частными X11/D-Bus/PulseAudio:

```sh
qt/tests/run_tests.sh BUILD_DIR MEDIA_DIR -R '^progressive-rf64-sparse-large-playback$' -V
```

На Windows Qt используются тот же CTest и отдельные профили. Classic:
`tests/windows/progressive_family_test.py --rf64-sparse-large-bits 16` и `24`
с обязательными обычными аргументами private player, fixtures и report.
Для временной Classic-копии до запуска обязательны
`ContextMenuPrompted=1`, `ContextMenuEnabled=0` и сравнение HKCU до/после.

Первый Windows-прогон завершился CTest exit 8 до открытия media: CRT truncate
физически выделил дыры. Fixture исправлен: FSCTL_SET_SPARSE, затем прямые
SetFilePointerEx/SetEndOfFile. Проверка выделенных областей после исправления
прошла. Начальная ошибка сохранена в отчёте; код плеера из-за неё не менялся.

Справка Microsoft по [FSCTL_QUERY_ALLOCATED_RANGES](https://learn.microsoft.com/en-us/windows/win32/api/winioctl/ni-winioctl-fsctl_query_allocated_ranges):
проверка выделенных областей отражает физическое размещение, а не готовность
media с точки зрения writer. Последняя отдельно определяется ranges и SHA.
