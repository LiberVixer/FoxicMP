# Непрерывное воспроизведение WebM с Vorbis

Windows Classic, Windows Qt и Linux Qt воспроизводят растущий WebM с одной
дорожкой VP8, VP9 или AV1 и одной обязательной дорожкой Vorbis. Рабочий
процесс копирует кодированные пакеты в приватный поток плеера, читая
только подтверждённый SHA‑256 префикс исходного файла.

| Видео | Режим `continuousRemuxStart.mode` |
|---|---|
| VP8 | `continuous-webm-vp8-vorbis-v1` |
| VP9 | `continuous-webm-vp9-vorbis-v1` |
| AV1 | `continuous-webm-av1-vorbis-v1` |

Клиент SDK 1.110 проверяет возможности через
`{"version":1,"action":"continuousWebmCapabilities"}`. Оба трека schema 2
ссылаются на один локальный ресурс известной длины. Для обоих требуется
`containerHint: "webm"`, явный `codecHint` и `required: true`.
`timeline.duration` задаёт конечную точку видео в единицах по 100 нс
с точностью до миллисекунды. Допустим начальный сдвиг видео относительно
аудио до 100 мс, сохранённый из файла.

Поддерживаются конечные и неизвестные размеры кластеров и переменная
частота кадров, в том числе видео SimpleBlock и BlockGroup. Для SimpleBlock
длительность кадра определяется по следующей временной метке. У BlockGroup
она задана явно. Для Vorbis mono/stereo 48 кГц используются соседние окна
кодека. Проверяются непрерывность звуковых отметок с допуском 1 мс,
целочисленная задержка в сэмплах в CodecDelay и последний DiscardPadding.
Прежние механизмы декодеров обеспечивают предчтение для перемотки.
[Спецификация Vorbis I](https://xiph.org/vorbis/doc/Vorbis_I_spec.html) ·
[Описание A_VORBIS в Matroska](https://www.matroska.org/technical/codec_specs.html#a_vorbis)

Пределы соответствуют continuous WebM SDK 1.109. Вход до 128 МиБ,
выход до 64 МиБ, заголовок до 1 МиБ, кластер до 16 МиБ, всего до 50 000
пакетов. Режим рассчитан на локальный VOD с постоянной конфигурацией
дорожек, без lacing и шифрования.

Для создания синтетического корпуса выполните из корня проекта:

```bash
PYTHONPATH=qt/tools python3 tests/progressive_continuous_webm_video_test.py \
  --prepare --vorbis --corpus /tmp/foxicmp-vorbis-corpus
```

Передайте путь к корпусу в `FOXICMP_TEST_CONTINUOUS_VORBIS_CORPUS` при
настройке CMake. Проверки Linux запускаются через `qt/tests/run_tests.sh`.
