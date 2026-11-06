# Continuous AAC: публичный API и явный SDK opt-in

6 ноября 2026. Следующий этап после [decoder adapters](ProgressiveContinuousAacDecoder.md).
Статус проверок и границы поставки: [отчёт](ProgressiveContinuousAacApiValidation-2026-11-06.json).
SDK 1.84 пока является кандидатом исходников; установленные приложения и опубликованные
пакеты SDK 1.83 не заменены.

## Контракт

Новый явно выбираемый профиль `continuous-h264-aac-window-v1` объявлен внутри
`continuousRemux.profiles`. Прежний default остаётся H.264 video-only без B-frames.
AAC допускает один общий scalar MP4 resource, ровно одну required H.264 video
и одну required AAC-LC audio track, mono/stereo 44,1/48 kHz. Прежние ограничения
SHA/ranges, duration, памяти, output cache и одного worker сохраняются.
HE-AAC, SBR, PS, дополнительные дорожки и произвольные конфигурации не объявлены.

```python
from progressive_continuous import ContinuousRemux, AAC_MODE
job = ContinuousRemux(client, session_directory, mode=AAC_MODE)
job.prepare(snapshot, resource_id)
# Публикации подтверждённых ranges и heartbeat делает вызывающая программа.
# После status()['playbackReady']:
job.play(title='Видео')
```

До проверенных initialization/fragment/PCM window playbackReady остаётся false.
`audioWindowReady`, rate/channels и presentationSamples относятся к проверенному
плану кэша; `pcmWindowApplied` — к фактическому decoder adapter. Одно не заменяет
другое. SDK проверяет полную вложенную capability и не переключает старые плееры
на AAC автоматически. Stop/error/cancel сохраняют прежние reader receipts и forget.
Seek разрешён после outputComplete; неподтверждённый seek остаётся недоступным.

Classic передаёт обе подготовленные проекции в открытие графа. Подтверждённый
локальный EOF короткого AAC не удерживает буфер более длинного видео и не означает
завершение загрузки. Штатные пороги Classic 10/5 s сохранены.

У короткой AAC дорожки bounded FFmpeg probe может оставить неизвестный profile
или frame_size. Только точный canonical LC AudioSpecificConfig (1024 samples,
44,1/48 kHz, mono/stereo, без SBR/PS) разрешает восстановить недостающую геометрию.
Медиа payload и packet clocks затем проходят прежнюю проверку. Размер probe,
его deadline и доступ к неопубликованному хвосту не расширяются.

## Проверки и дальнейшая работа

Частный Qt host использует production Window, API, controller и Player с null audio.
На Linux применяется отдельный X11/D-Bus/PulseAudio через run_tests.sh; Windows Qt
работает offscreen. Classic проверяется обычным полным EXE в интерактивном сеансе
Windows, отдельными INI и Null Audio Renderer. SSH-сеанс без desktop не является
приёмкой его графа. Ни физический звук, ни точность физической A/V синхронизации
эти проверки не измеряют.

Новые IPC fixtures длятся 14,137 s и включают AAC длиной в один sample,
non-aligned окончания, fractional fps, B-frames и aligned контроль. Проверяются
prepare до initialization, явный play, pause при публикации, completed seek,
EOF, Stop, failure required audio, close/filesClosed/forget и неизменность исходника.
Отдельная worker regression проверяет canonical ASC и пять неподдерживаемых конфигураций.

Открыты упакованные обычные main приложения Qt, поставка SDK/runtime 1.84,
установка новой Linux версии и полная AAC waiting/crash/race/stress матрица.
Исторические latency outliers и остальная codec/container матрица остаются в
[общем чеклисте](ProgressiveCompletionChecklist.md). Harvester не меняется.
