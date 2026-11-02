# Progressive RF64 PCM16/PCM24

SDK 1.77, приложение 0.2.0. Два отдельных RF64-профиля для Classic,
Windows Qt и Linux Qt. Прежние RIFF/WAV capability не расширяются неявно;
Harvester не изменяется.

## Согласование

Проверить capability фактического EXE:

| PCM | Capability в `schema2Playback.standalone` | Mode | Container hint | Codec hint |
| --- | --- | --- | --- | --- |
| 16 | `rf64Pcm16` | `standalone-rf64-pcm16-v1` | `rf64-pcm16` | `pcm-s16le` |
| 24 | `rf64Pcm24` | `standalone-rf64-pcm24-v1` | `rf64-pcm24` | `pcm-s24le` |

У каждого `version=1`, один profile без суффикса `-v1`. Одна обязательная
audio-дорожка, один resource и непрерывный подтверждённый prefix с SHA-256.
Duration VOD конечная и известная, `timestampOffset=0`. Начальная длина
resource может быть неизвестна. Примеры:
[PCM16](../tests/fixtures/progressive-v2/rf64-pcm16.json),
[PCM24](../tests/fixtures/progressive-v2/rf64-pcm24.json).

## Проверяемый профиль

- Little-endian `RF64`, тип `WAVE`, RIFF size sentinel `0xffffffff`.
  Первый chunk — `ds64` ровно 28 bytes: 64-bit RIFF/data/sample sizes,
  нулевая длина дополнительной size table. Значения известные и ненулевые;
  RIFF size не включает первые восемь bytes файла.
- Один последний непустой `data` с sentinel `0xffffffff` и фактическим
  размером из ds64. Количество audio frames равно ds64 sampleCount;
  конечная граница data с RIFF pad совпадает с границей RF64.
- PCM16: format tag 1, fmt16 либо fmt18 с cbSize 0. PCM24: format tag
  65534, fmt40/cbSize22, PCM GUID, container/valid bits 24, mono mask 4
  или stereo mask 3. Оба профиля: mono/stereo, 44.1/48 kHz.
- Максимум 16 GiB на файл и 250000 packets; каждый packet содержит до
  16384 audio frames. Фактический предел определяется обоими ограничениями
  и числом bytes/frame. `JUNK`, `LIST`, `bext` до data ограничены 1 MiB и
  64 chunks; одна публикация не превышает 16 MiB.

RF64/ds64 framing основан на
[EBU Tech 3306 v1.1](https://tech.ebu.ch/docs/tech/tech3306v1_1.pdf).
Это ограниченный совместимый RF64-профиль: BW64, дополнительные size-table
entries, неизвестные ds64 sizes, live recording, float, другие masks,
valid bits/rates/layouts и несколько data chunks не включены.

## Передача и время

Общий PCM parser использует RF64-специализацию. Сложения размеров
ограничены до построения virtual view; несогласованные ds64 sizes и
sampleCount отклоняются. Виртуальный fMP4 использует проверенные `sowt`
для PCM16 и `in24`/QuickTime v1 с `wave/enda=1` для PCM24. Исходные PCM
bytes не меняются и не перекодируются; физические чтения идут через
ResourceSource/SHA. Полная копия медиа не создаётся.

Неполные ds64/fmt/первый packet дают `waiting_initialization` без утечки
reader. Поздние неполные packets ждут update. Prefix до writer complete
не становится EOF. RIFF pad не входит в PCM. Pause/seek/update сохраняют
reader; последний владелец освобождает его. Clock и packet index строятся
по числу audio frames и sample rate. Точный clock не означает sample-exact
UI seek или bit-exact режим физического устройства.

## Совместимость обнаружения Qt

Ответ `--progressive-api-info` теперь сериализуется компактным JSON:
RF64 capability вывели прежний формат с отступами и Windows CRLF за
64 KiB. Поля и лимит клиента сохранены. Регрессия проверяет фактический
CLI output, отсутствие создания профиля при discovery, autostart,
capabilities через socket/pipe и отказ команде больше 65536 bytes.
CLI Python выдаёт success/error JSON с ASCII escapes для Unicode, чтобы
перенаправленный вывод не зависел от кодовой страницы Windows. Значения
строк после JSON decode сохраняются; wire requests по-прежнему UTF-8.

## Проверки и границы доказательства

[Conformance](../tests/progressive_rf64_conformance.py) использует восемь
growing rate/channel/bit-depth profiles с RF64 writer FFmpeg, независимый
manual writer и Python PCM oracle. Проверяются packet bytes/clocks,
точный PCM, ds64/sentinel/sample-count errors, overflow, partial headers,
SHA, RIFF padding, EOF и release.

Дополнительно [64-bit geometry oracle](../tests/progressive_rf64_geometry.h)
проверяет production layout, FragmentIndex, late seek и точное чтение по
смещениям за 4 GiB для всех восьми сочетаний. Источник синтетический,
физический файл на 4 GiB не создаётся. Это доказательство адресации и
индексации; сквозной playback физического файла больше 4 GiB и длительная
нагрузочная проверка пока остаются открытыми.

Итоги трёх редакций и санитайзеров — в
[отчёте](ProgressiveRf64Validation-2026-11-02.json).
Полная audio matrix, большие реальные RF64, sparse/live/segments,
длительный stress и реальная интеграция Harvester остаются в
[общем чеклисте](ProgressiveCompletionChecklist.md).
