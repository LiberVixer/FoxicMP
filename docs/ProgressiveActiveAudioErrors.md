# Ошибки активного optional audio — SDK 1.29

Classic, Windows Qt и Linux Qt изолируют обнаруженную ошибку отдельной
необязательной аудиодорожки schema 2, в том числе после подключения к декодеру.
YouTubeHarvester не менялся. Требуется новая сборка плеера; SDK ZIP сам по себе
не обновляет установленные Windows EXE.

## Поведение

Ошибочная дорожка сохраняет стабильные ID и index, получает `state:error`,
`ready:false`, `errorCode` и `error` в `mediaTracks`. Пока она не выбрана,
основное воспроизведение продолжается. Если она выбрана, плеер переходит на
обязательную primary audio (`index:1` медиамоста). Это индекс источника;
runtime ID mpv/AudioSwitcher определяется отдельно.

Сохраняются позиция, пользовательская пауза, выбранные субтитры и reader identity.
Qt переоткрывает backend streams без повреждённого источника; Classic выполняет
stop/flush, выбирает обязательный поток и восстанавливает позицию. Переключение
может сопровождаться короткой паузой. Ошибочный пункт меню недоступен, циклический
выбор его пропускает. После ошибки работают seek, последующие публикации и EOF.

Дорожка остаётся в карантине до закрытия сеанса. Восстановление её байтов или
новая revision не возвращают её в декодер; для повторного подключения нужен
новый сеанс. Плеер не удаляет и не переписывает файлы загрузчика. Пин и консервативная
reader lease могут сохраняться до закрытия сеанса; release подтверждается после
закрытия файлов. Отказ одного leaf не разрешает writer удалять другие ресурсы.

## Область изоляции

Изолируются ошибки отдельного optional fMP4/WebM audio: сокращение подтверждённого
файла, обнаруженная перезапись initialization/последнего проверенного чтения,
отказ или отмена track/resource загрузчиком и ошибка проверки индекса при update.
Поддерживаются прежние AAC-LC/Opus/Vorbis профили. Ошибки ещё не подключённой
дорожки также остаются локальными.

Required video/audio, корневой отказ writer, неправильные метаданные, отзыв ranges,
смена track/resource identity и некорректный JSON сохраняют строгую проверку.
Произвольные payload-повреждения, которые не обнаруживаются существующей проверкой
и декодером, не покрываются этим расширением. Полное хеширование всех пакетов и
универсальная изоляция внутренних сбоев backend ещё не реализованы.

## Capabilities

```json
{"optionalAudio":{"completeFileRequired":false,"selectedTrackBuffering":true,"minimumReadySeconds":5,"lateCompletion":true,"pendingErrorIsolation":true,"activeResourceErrorsFatal":false,"activeOptionalAudioErrorsIsolated":true,"requiredPrimaryAudio":true}}
```

Обычный `ResourceSource` по-прежнему использует Strict. Старая политика pending
сохраняет переход активной audio в strict; новая политика включена только в
ограниченном schema-2 медиамосте. Общие API, v1 и формат schema 2 не изменены.

Проверки: [отчёт](ProgressiveActiveAudioValidation-2026-10-29.md).
Qt: CTest `progressive-active-audio`; Classic: отдельная тестовая копия,
`tests/windows/progressive_family_test.py --active-audio-only`.
