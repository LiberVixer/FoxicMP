# Проверка медиапрофиля v2 — 26 октября 2026

[Профиль SDK 1.6](ProgressiveMediaV2.md). Три редакции собраны и проверены отдельно;
YouTubeHarvester не изменялся. Это первый сквозной профиль schema 2, **не** вся матрица ТЗ.
Базовый commit: `18cdf50bc15745ee6389ec3407d8d6cb7c5db696`.
[Машинный отчёт с хешами и ответами API](ProgressiveMediaValidation-2026-10-26.json).

## Один wire-сценарий на всех трёх плеерах

`tests/progressive_v2_playback_conformance.py`, клиент/SDK из поставки:

1. Физически записаны только первые 5 секунд каждой дорожки, Unicode-имена.
2. Probe поддержан и не оставляет reader; start возвращает точный generation
   `18446744073709551615` и revision `9007199254740993` (без double).
3. Повтор start идемпотентен; другой writer.instanceId отклоняется.
4. Release сохраняет точные поля, подтверждается закрытие; повторный start создаёт
   другой reader, команды старого reader отклоняются.
5. Дописываются только первые 12 секунд, публикуется следующая revision.
6. До дописывания остатка фиксируются playing, position >=10 и фактические
   ResourceSource payload reads обеих дорожек. Размеры на диске: **392908/659273**
   байтов video и **108398/180489** audio — оба файла ещё недокачаны.
7. Оба файла переименовываются; через те же OS handles читается дописанный остаток.
   Complete с revision `9007199254740995`, естественный finished, released=true,
   positive files-closed receipt; после acknowledge список readers пуст.

| Редакция | Секунды до complete | Payload bytes video / audio в этот момент | Завершение | Exit |
|---|---:|---:|---|---:|
| Linux Qt | 10.000000 | 392908 / 108398 | finished, released=true | 0 |
| Windows Qt | 10.040000 | 392908 / 108398 | finished, released=true | 0 |
| Windows Classic | 10.055989 | 389165 / 104847 | finished, released=true | 0 |

`decoderIoBytes` подтверждает прохождение медиаданных через общий I/O-слой;
playing/позиция и естественный EOF проверены на реальных медиадвижках.
Эти проверки не измеряют физический A/V sync, качество звука или видимый пиксельный кадр.
Сеть/writer Harvester не использовались.

## Regression и отрицательные случаи

- Linux Qt: 5 CTest-групп (model/readers/resources/media/API), 20 Qt API проверок,
  exit 0. Частные X11/D-Bus/PulseAudio. После финального изменения release-ответа
  повторён целевой media-сценарий, exit 0.
- Windows Qt: 4 исходные regression-группы, exit 0; финальная сборка + новые native
  media и сквозной v2 playback, exit 0. Отдельные профили, offscreen.
- Classic: DSUtil + BaseSplitter + MP4Splitter + приложение, exit 0. Интерактивная
  одноразовая тестовая копия: v2 metadata, readers, 11 resource checks с сохранением
  активного v1 playback, 100 RPC, прежний SDK writer и новый v2 media, exit 0.
  Процесс завершён с 0; HKCU команды/иконки установленного Classic совпали до/после.
  Тестовый INI заранее отключал регистрацию. Одноразовая задача удалена, LastTaskResult=0.
- 17 native media случаев: Linux GCC, Windows MinGW и MSVC, exit 0; ASan/UBSan,
  включая leak detection, те же 17, exit 0. Реальные codec headers, AAC-LC,
  внешние ссылки/шифрование, ограничения clock/container, ожидание init,
  confirmed-range short read, EOF только complete, rename/подмена имени,
  неизменность init и постоянная ошибка после rewrite, ошибочный generation
  не заменяет источник, leases освобождаются последним владельцем.
- Python SDK: 32 проверки (12 новых v2), exit 0. Lost start без ложного released,
  старые capabilities, reader guards, повторный close и точные счётчики.
  Семейный контракт: 2 проверки, exit 0.

Первый native harness прогон завершился с 1: тест опрашивал ещё не созданный registry;
исправлена инициализация стенда. Первый Classic прогон завершился с 1: probe создавал
лишнюю групповую квитанцию; для probe отключена регистрация группы. После исправлений
соответствующие сценарии повторены и прошли. Ошибки первых прогонов не скрыты.

## Установка и оставшиеся границы

Проверенный Linux EXE заменён атомарно, предыдущий сохранён для отката.
SHA-256 нового: `9dcbec9ddf7fd537fa1f2b8e51a67b46060de0d8b824435b83d4e5175ad225a1`.
INI до/после: `ac9387b8d09f63317ed8ea3b681abad96e5abe8890400d4db6540017e3acde75`.
Настройки/личные файлы и текущий просмотр не изменялись. Windows установленным
приложениям EXE/профили не заменялись; проверки на отдельных копиях.
Номер продукта остаётся 0.2.0, новую поддержку определять по capabilities/хешу.

Остаются: другие форматы/кодеки и универсальный demux/remux, metadata/timeline/live,
полная integrity-модель, перенос тяжёлой индексации из UI, реальные источники и
длительный drift/resource test. Wayland и оборудование не возобновлялись.
SDK ZIP не является обновлением установщиков 0.2.0 и не содержит EXE.
