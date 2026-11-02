# Classic: внутренний PCM-адаптер

Следующий шаг после [Qt gapless SDK 1.68](ProgressiveMp3Gapless.md).
Внутренний `CFFAudioDecoder` Classic умеет выбирать точное полуоткрытое окно
декодированных MP3 samples до преобразования выходного PCM-формата.
[Результаты от 2 ноября 2026](ProgressiveClassicPcmWindowValidation-2026-11-02.json).

Это проверенный компонент, а не включённый progressive-профиль Classic.
Его capabilities/API по-прежнему отвечают отказом на `mp3-gapless`.
SDK остаётся 1.68, приложение — 0.2.0. Harvester и установленные Windows
приложения не изменяются.

## Явные границы и жизненный цикл

`CFFAudioDecoder::SetPcmWindow(begin, end, rate, decodeFirst)` требует
инициализированного внутреннего MPEG Layer III decoder, mono/stereo,
44,1/48 kHz и отсутствие предварительного mixer. Вызывающая сторона должна
проверить metadata и CRC, выбрать этот decoder и передать точный абсолютный
номер первого декодируемого sample после preroll. Вывод позиции из округлённых
DirectShow timestamps не используется. Некорректная замена не изменяет
уже принятое окно.

`HarvesterPcmWindow.h` отслеживает абсолютный курсор и возвращает число samples
для пропуска/сохранения. Время вывода считается от начала выбранного окна
целочисленно, в 100 ns. Планарные каналы копируются отдельно с правильным
шагом, interleaved PCM — по целому sample frame. Обрезка не ресэмплирует звук.

Flush и переинициализация очищают окно. Ошибка декодирования или смена rate
при активном окне закрывает этот путь с ошибкой; автоматического перехода
на необрезанный PCM после ошибки нет. `FinishPcmWindow()` отклоняет EOF до
достижения заданного конца; фильтр передаёт ошибку дальше. Окно само не
создаёт EOF и не решает, закончил ли writer запись исходного файла.

## Проверка

На Windows probe использует реальные `CMpaDecFilter` и `CFFAudioDecoder`,
собранные MSVC с теми же статическими библиотеками, что и Classic. Звуковой
renderer и физическое устройство не открываются. Профиль probe лежит в
отдельной папке; prompt контекстного меню отключён до запуска.

40 проверок для четырёх настоящих libmp3lame 3.100 fixtures:

- Mono/stereo × 44,1/48 kHz, обычный decode без окна.
- Сравнение с независимым native MP3 float decode для проверки положения
  encoder delay + decoder delay 529. Допуск сравнения разных сборок FFmpeg —
  `1e-5`; фактический максимум сохранён в JSON.
- Побайтовое сравнение окон с полным PCM того же Classic decoder: начало,
  sample 1, `2*rate+1`, последний sample; перемотка с frame-aligned preroll
  не менее 250 ms. Точные длины, SHA-256 и timestamps проверены.
- Flush и replacement не переносят trim. Неинициализированный и другой
  decoder, неверные rate/границы/preroll отклоняются.
- Усечённый вывод не выдаётся за успешное окончание. Исходник сохранён.

Scalar-компонент отдельно проверяется на Windows MSVC и Linux: 1737 checks,
восемь окон, неравномерные размеры блоков, границы и переполнение. Linux
ASan/UBSan проверяет этот scalar-компонент; это не Windows decoder sanitizer.

В отдельной копии полного Classic повторены progressive indexed Info MP3,
plain MP3, ADTS и raw FLAC. Новый профиль по-прежнему отвергается без reader.
Shell registration проверяется до/после, одноразовая задача и staging удаляются.
Qt runtime и его бинарники этим Classic-этапом не меняются; установленная
Linux-сборка остаётся проверенной версией SDK 1.68 с сохранённым профилем.

Повторение: в x64 MSVC Developer Command Prompt после сборки зависимостей
Classic запустить `tests\windows\build_classic_pcm_probe.cmd`, затем:

```
python tests/progressive_classic_pcm_adapter.py --runner _bin/classic-pcm-probe/probe.exe
```

Scalar: собрать CMake target `foxicmp-classic-pcm-window-tests` и выполнить
CTest `progressive-classic-pcm-window`. Для Windows decoder fixture нужны
Python 3.10+, FFmpeg/FFprobe и libmp3lame в PATH тестового процесса.

## Что ещё нужно для Classic gapless

1. Передача проверенного плана в graph и гарантированный выбор внутреннего
   decoder; отказ внешнему decoder без совместимого контракта.
2. Повторное вооружение окна на каждом seek/flush с точным номером первого
   MP3 frame; согласование NewSegment, полезной шкалы, chapters и status.
3. Проверка цифрового PCM после настоящего DirectShow graph, обычной обработки
   звука, growing waits, pause/seek/complete/EOF/cancel и release.
4. После этих доказательств — отдельная capability Classic и обновление SDK.

Проверки компонента не закрывают эти пункты и не подтверждают физический
выход, A/V drift, другие MPEG profiles, Wayland или реальную интеграцию Harvester.
