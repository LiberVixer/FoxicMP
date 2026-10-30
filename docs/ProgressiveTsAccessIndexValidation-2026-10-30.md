# Проверка TS AVC/AAC access units и индекса — 30 октября 2026

Это внутренний компонент, не TS playback. SDK 1.38, Source/движки и установки
не менялись. [Профиль и ограничения](ProgressiveTsAccessIndex.md).

| Среда | Итоговые сценарии | Exit code |
|---|---|---|
| Linux GCC, CTest | 74 access/index + 65 прежних transport | 0 |
| Windows MinGW, CTest | Те же 74 + 65 | 0 |
| Windows MSVC, native | Те же 74 access/index | 0 |
| Linux ASan/UBSan, leak detection | Те же 74 access/index, без ошибок sanitizers | 0 |

Каждый access/index-прогон включает 512 мутаций AVC заголовков/payload:
499 отклонены, 13 сохранили framing. Принятие не доказывает пригодность для
декодера. Для прежнего transport-прогона отдельно сохранены его 512 мутаций.

Семь реальных локальных фикстур FFmpeg: Baseline 25 fps, Main с B-кадрами,
High с B-кадрами 30000/1001 fps, AAC 48 kHz mono/stereo, 44.1 kHz stereo,
32 kHz mono. В каждом прогоне проверены 750 access units и 24 последовательных
публикации сегментов. Байты всех encoded units, PTS/DTS и video durations
сверены с ffprobe; для AAC отдельно сверены payload hashes без ADTS header.
Размеры видео, crop, keyframe и audio rate/channels также совпали.

Синтетические сценарии проверяют SPS/PPS, profile restrictions, RBSP/Annex B,
AUD и slices, SEI/filler framing, bundled AAC и все шесть сочетаний rates/channels,
нецелые/неподдерживаемые заголовки, постоянство конфигурации и declared envelope.
Индекс проверяет IDR/audio seek, prefix readiness и virtual byte position.
Ошибка duration, конфигурации, перехода или общего clock не меняет прежний
frame count, byte position и готовую границу. 65537 access units в одном сегменте
отклоняются; фактический глобальный предел 2 млн не испытывался.

При проверке устранён риск накопления округления: одного сравнения соседних
кадров с допуском 1 TS tick недостаточно. Новые проверки сравнивают DTS и
presentation cadence с единым началом и суммарным числом кадров как внутри
сегмента, так и между сегментами. Тесты с постепенным сдвигом на один tick
обнаруживают накопленный дрейф; ошибочный сегмент оставляет индекс прежним.
Рациональная арифметика делит числитель до умножения, сохраняя диапазон 64 bit.

Первая GCC-компиляция завершилась 1 из-за общего auto для int/unsigned;
объявление исправлено. Все итоговые сборки и проверки завершились 0. Полные
результаты, коды и SHA-256: [JSON](ProgressiveTsAccessIndexValidation-2026-10-30.json).

Плееры не пересобирались и не запускались. Их три build EXE, Linux installation
EXE и INI совпадают по SHA-256 с доказательством установки SDK 1.38. Harvester,
Windows-установки и shell registration не затрагивались. Тесты работают с
частными временными файлами, CPU-генераторами FFmpeg и native executable;
UI/audio sinks, hardware, Wayland и планировщик не использовались.

Остаются ResourceSource/staging, виртуальное чтение и действительные декодеры
трёх редакций с buffer/seek/complete/release, затем capability/SDK. Проверки
не доказывают entropy decoding, многочасовую A/V sync или реальные сетевые
источники. VFR/POC1/interlaced, muxed и live/config changes не включены.
