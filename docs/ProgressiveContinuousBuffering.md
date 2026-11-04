# Qt continuous-remux: статус ожидания данных

Исправление относится к Linux Qt и Windows Qt. Classic уже выдаёт `buffering`
при ожидании недокачанного хвоста; его код, пороги буфера и установленный профиль
в этом этапе не меняются. Контейнеры/кодеки и ограничения SDK 1.82 сохраняются.

## Причина и поведение

В проверке [10/30/60 секунд](ProgressiveContinuousWaiting.md) позиция Qt стояла,
producer ожидал ranges, но публичный playback status оставался `playing`.
`Window::preparedClipStatus` после загрузки проверял только `pause`. Признак
mpv `paused-for-cache` уже был выставлен и учитывался строкой состояния, но
пропускался этим API. Это воспроизведено новым тестом на прежнем установленном
EXE: CTest exit **8**, ожидаемое `buffering`, фактическое `playing`.

Теперь загруженный continuous decoder с `pause=false` и `paused-for-cache=true`
даёт `state=buffering`. Ручная пауза имеет приоритет: `pause=true` даёт `paused`
в API, строке состояния и шаблоне заголовка окна, даже если признак ожидания
ещё выставлен. Публикация данных не снимает ручную паузу. После явного Play и
фактического возобновления decoder возвращается `playing`.

Решение использует [документированный сигнал mpv](https://mpv.io/manual/stable/#property-list)
и проверяется на реально используемых libmpv обеих ОС. Остановка позиции,
ожидание producer или блокирующий callback сами по себе не считаются
буферизацией: декодер может ещё проигрывать уже прочитанные кадры. Новые таймеры,
искусственная пауза, перезапуск decoder и смена thresholds не добавляются.

## Дополнительное поле Qt API

Исправленные Qt-сборки добавляют `reasonCode` к ответам continuous playback.
Это дополнение совместимого API, а не новое обязательное поле SDK/Classic.
Ранние сборки SDK 1.82 могут его не содержать; клиенту нужен безопасный fallback.

| `state` | `reasonCode` | Значение |
| --- | --- | --- |
| `buffering` | `decoder-opening` | Decoder ещё не загрузил источник |
| `buffering` | `waiting-range` | Decoder остановился при неполном output |
| `buffering` | `decoder-buffering` | Output завершён, decoder ещё буферизуется |
| `paused` | `paused` | Воспроизведение удерживается паузой |
| `playing` | пустая строка | Decoder воспроизводит источник |
| `finished` | `source-complete` | Подтверждённый конец завершённого output |
| `closed` | `playback-closed` | Попытка остановлена или заменена |
| `error` | код producer либо `decoder-error` | Ошибка сохраняет свой статус |

Например, подтверждённая ошибка writer остаётся `writer-failed`; после Stop
не остаётся устаревшая причина `waiting-range`.

## Проверка

[Отчёт, SHA-256 и точные exit codes](ProgressiveContinuousBufferingValidation-2026-11-04.json).
Проверяются обе Qt ОС отдельно:

- `continuous-player`: новое настоящее Window/API испытание проверяет начало
  проигрывания, сигнал mpv, API/label buffering, ручную паузу, append/complete,
  сохранение позиции, явный Play и подтверждённый EOF. Проверяются текст виджета
  `playbackStatus` и API, а не только выбор параметра движка.
- Публичный SDK: реальные ожидания writer 10/30/60 s требуют `buffering` и
  `waiting-range`. Ручная пауза требует `paused`, продолжение — пустую причину,
  Stop/Cancel — `playback-closed`. Сохраняются worker, playback и reader IDs.
- Повторный обычный IPC playback прогон проверяет `writer-failed`, старые
  playback/release/replace/lifecycle сценарии и фактическое закрытие файлов.
- Прежние SDK preparation/playback, readers, retirement, registry UI,
  controller и bounded remux regression остаются в CTest прогоне.

Linux использует частные X11/D-Bus/PulseAudio. Windows Qt использует отдельные
профили и software/offscreen тестовый вывод. Завершение idle тестовых процессов
не считается доказательством приёмки normal shutdown/crash. Harvester не меняется.

Положительный результат закрывает найденное расхождение Qt status/индикации
только в ограниченном continuous MP4/H.264 video-only профиле. Полные reasonCodes
всей матрицы, heartbeat expiry, shutdown/crash и длительная stress приёмка,
аудио/AAC trim и B-frames остаются открытыми.
