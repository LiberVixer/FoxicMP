# Optional audio — валидация 2026-10-29

Профиль SDK 1.25 проверен player-only в трёх редакциях. Harvester не изменялся.

- Linux Qt: CTest `progressive-optional-audio`: 1/1 PASS, 9 профилей; native media 463/463 PASS.
- Windows Qt: optional-audio сценарии: 5/5 PASS; полный configured API/resource/media прогон: 80/80 PASS.
- Windows Classic: 8/8 optional-audio профилей PASS в отдельной тестовой копии; тестовая задача и stage удалены после завершения.
- Проверены late AAC/Opus/Vorbis, initial-ready, missing/invalid/failed/never-ready и выбор с перемоткой.

Обновление Linux-плеера выполняется атомарно после сборки; установленный INI и личные файлы сохраняются. Windows Classic и Windows Qt установки не изменяются.
