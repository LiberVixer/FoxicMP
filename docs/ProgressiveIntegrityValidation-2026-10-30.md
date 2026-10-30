# Проверка SHA-256 целостности progressive-ресурсов — 30 октября 2026

SDK 1.30, player-only изменения. YouTubeHarvester и установленные Windows-приложения
не изменялись. Проверка выполнялась отдельными профилями и временными каталогами.

| Проверка | Linux Qt | Windows Qt | Windows Classic |
|---|---:|---:|---:|
| Потоковая SHA-256, включая границы padding и 1 MiB | PASS | PASS | через общий reader contract |
| Повреждение до первого чтения | PASS | PASS | PASS |
| Повреждение уже прочитанного блока | PASS | PASS | PASS |
| Добавление новых подтверждённых блоков | PASS | PASS | PASS |
| Неизменность опубликованных digest | PASS | PASS | PASS |
| Изоляция optional audio | PASS | PASS | PASS |
| Required audio остаётся fatal | PASS | PASS | PASS |
| Совместимость v1 и прежних профилей без `integrity` | PASS | PASS | PASS |
| Полный media/native corpus | PASS | PASS | PASS |

Linux native integrity/resource/model: exit 0. Linux Qt integrity, active audio,
confirmed seek, metadata UI и resource regression: exit 0. Windows Qt integrity
профили: 6/6 PASS, media corpus: 1/1 PASS, совместимость v1: exit 0. Classic
четыре integrity audio-профиля и v1 directory scenario: PASS, задача и временная
копия удалены после проверки. Python SDK tests: 28/28 PASS. Family contract: PASS.

Контрольные суммы проверяются до выдачи блока декодеру; ошибка сообщает
`integrity-mismatch`. Изменение digest в следующем snapshot отклоняется, а новые
подтверждённые блоки могут добавляться. Ограниченный круговой аудит проверенных
64 KiB частей обнаруживает последующую перезапись, но не является мгновенным
полным сканированием всего файла. Ресурсы без `integrity` сохраняют прежнюю
проверку identity/size/last-read. Внешний индекс для больших файлов и универсальная
изоляция любых внутренних ошибок decoder ещё не реализованы.

## Проверенные сборки

- Linux Qt: `b3cb971211a5d3a0bd00623b8e2c348f17383a1f265bdede946c44097e843a6e`
- Windows Qt: сборка `_bin/qt-release-0.2.0-win4`, integrity/media exit 0
- Classic: `916efc971426041f2938d9df9df65fc95011acac22d6a2a52ee0deec193a3eb6`
- Native media fixture: результат `integrity-media-native.json`, exit 0

Linux EXE заменён атомарно после всех проверок. Предыдущий EXE сохранён в
`foxicmp-qt.before-sdk-1.30-20261030T013717Z`; профиль FoxicMPQt.ini не изменился
(SHA-256 до/после `ee2a51243880e578a7b1f38a0278ed178d1e2e379ba0f4f756b5f77427ac24b6`),
188 существующих файлов установки совпали.
