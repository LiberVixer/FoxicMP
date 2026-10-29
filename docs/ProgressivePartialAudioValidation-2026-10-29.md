# Проверка частично скачанной выбранной audio — 2026-10-29

SDK 1.28 проверяет player-only сценарий во всех трёх редакциях. YouTubeHarvester
не изменялся; тесты публикуют снимки через локальный schema-2 writer.

| Область | Результат | Exit |
|---|---:|---:|
| Linux Qt: partial AAC/Opus/Vorbis + fMP4/WebM, выбор, дозагрузка, возврат, pause/seek | 6 QtTest PASS | 0 |
| Linux Qt: поздние SRT и partial-audio regression | 2 CTest groups, 9 cases PASS | 0 |
| Windows Qt: partial AAC/Opus/Vorbis + fMP4/WebM и поздние SRT | 2 CTest groups, 9 cases PASS | 0 |
| Windows Qt: metadata, optional-audio, confirmed-seek regression | 3 CTest groups, 28 cases PASS | 0 |
| Classic: partial AAC/Opus/Vorbis/fMP4 profiles and writer-liveness | 4 profiles PASS | 0 |
| Native media/resource corpus | 465/465 PASS | 0 |
| Family contract | PASS | 0 |

Проверены состояния `waiting`, `ready`, `buffering`, `playing`, `paused` и
`waiting-downloader`; подтверждены reader identity, отсутствие ложного EOF,
дозагрузка того же ресурса, возврат на required audio во время ожидания,
перемотка назад и восстановление субтитров. Для Windows и Classic использованы
отдельные тестовые копии и профили. Установленные Windows-приложения и их профили
не изменялись.

Граница профиля: после активации optional audio повреждение опубликованного
ресурса всё ещё может завершить A/V-сеанс; полная integrity-изоляция остаётся
следующим этапом. HLS/DASH, live, дисковые меню, оборудование и Wayland не
проверялись по плану.

Исходные проверки: `_bin/partial-audio-linux-subtitles.log`,
`_bin/partial-audio-win-subtitles-remote-test.log`,
`_bin/partial-audio-win-verified-regression-remote-test.log`,
`_bin/partial-combined-classic`, `_bin/partial-native.json`.
