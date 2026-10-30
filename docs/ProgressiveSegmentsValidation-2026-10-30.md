# Проверка сегментного timeline — 30 октября 2026

SDK 1.31. Это этап общего контракта schema 2, без включения segment playback.
YouTubeHarvester и установленные Windows-приложения не изменялись.

| Сценарий | Linux Qt | Windows Qt | Classic/common |
|---|---:|---:|---:|
| Точная sequence и ссылки на resources | PASS | PASS | PASS |
| Пересечение после timestampOffset | PASS | PASS | PASS |
| Переполнение presentation timestamp | PASS | PASS | PASS |
| Discontinuity + новая initialization | PASS | PASS | PASS |
| Изменение configuration без init отклоняется | PASS | PASS | PASS |
| Добавление сегмента в новую revision | PASS | PASS | PASS |
| Live window сдвигается вперёд | PASS | PASS | PASS |
| Live window назад отклоняется | PASS | PASS | PASS |
| Существующие resources/reader/integrity regression | PASS | PASS | PASS |

Linux CTest `progressive-segments`, `progressive-model`, `progressive-resources`
и `progressive-integrity-native`: 4/4 PASS, exit 0. Windows Qt та же группа:
4/4 PASS, exit 0; segment vector — 11 случаев. Python SDK tests — 28/28 PASS,
family contract — 2/2 PASS. Classic пересобран с тем же shared parser; его
совместимость проверяется общим C++-корпусом.

Проверяется как исходная шкала `start`, так и presentation-шкала
`start + timestampOffset`. Segment playback, demux/remux, staging байтов,
удаление вышедших live-сегментов и реальная HLS/DASH загрузка ещё не включены.
Capability `schema2Playback.segments=false` сохраняется.
