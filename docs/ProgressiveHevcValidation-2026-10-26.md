# Проверка progressive HEVC — 26 октября 2026

[SDK 1.14 и границы профиля](ProgressiveHevc.md). Базовый commit
`9afa50b93002c986a69842b7bfa264d3571a44e6`.
[Машинный отчёт: хеши EXE/фикстур, результаты и состояния](ProgressiveHevcValidation-2026-10-26.json).
YouTubeHarvester не изменялся.

## Итоговые проверки

| Стенд | Результат |
|---|---|
| Linux Qt | Полный progressive API: 65 PASS; native media: 40; legacy/source: 19; 3 CTest группы, exit 0 |
| Windows Qt | Полный progressive API: 66 PASS; native media: 40; 2 CTest группы, exit 0 |
| Classic | 8 SDK групп, теперь H.264/HEVC8/HEVC10 playback; async close 5, legacy playback, maintenance 3, opening 4; exit 0 |
| Native media ASan/UBSan | 40 PASS, leak detection включён, exit 0 |
| FragmentIndex ↔ FFprobe | 4 теста; HEVC Main/Main10 packet offsets/sizes/PTS/DTS/sync, старые H.264/AAC и отрицательные случаи; exit 0 |
| Python SDK / family contract | 35 / 2, exit 0 |

QtTest числа включают init/cleanup. Linux использует частные X11/D-Bus/PulseAudio.
Windows Qt — отдельные offscreen профили. Windows-набор не содержит Linux source
suite; это две группы, не заявленные три. Выбор hardware-параметра сам по себе не
считается проверкой ускорения.

## Что проверено до complete

Один Python conformance-сценарий выполняется во всех трёх редакциях для H.264,
HEVC Main 8-bit/hvc1 и Main10 10-bit/hev1:

1. Начальные пять фрагментов обеих дорожек; реальный probe без оставшихся leases.
2. Start, повторный start, защита writer/reader ID, cancel/release и новый reader.
3. Публикация двенадцати фрагментов. Позиция проходит десять секунд, пока **оба
   физических файла меньше полной записи**, writer всё ещё downloading.
4. Есть реальные decoder I/O bytes обеих дорожек; фактический videoCodec/mediaMode
   совпадает с профилем, точные generation/revision сохраняются.
5. Переименование закреплённых файлов, дописывание, complete, естественный finished
   и подтверждение освобождения всех readers.

Qt дополнительно снимает framebuffer при незавершённом источнике и проверяет,
что декодированные кадры меняются. Фактический backend video-format — hevc;
Main10 даёт decoder pixelformat yuv420p10. Эти проверки прошли на обеих ОС с
программным декодированием. Это проверка decoded framebuffer, не измерение
физического монитора. Classic подтверждён его DirectShow graph/status и чтением
декодера; оптическое измерение экранного вывода не выполнялось.

В Classic найден и исправлен второй H.264-only guard в MP4Splitter. Допуск HEVC
ограничен проверенным v2-мостом и fragmented movie; v1/legacy не расширяются
случайно. Ошибка открытия progressive-графа теперь передаётся в сохранённое
состояние сессии вместо простого stopped. Финальный Classic regression выполнен
после обоих исправлений; хеш тестовой копии совпал с итоговым EXE.

Native media корпус проверяет повреждённые hvcC lengths/arrays/headers, reserved
bits, отсутствующие VPS/SPS/PPS, неподдержанный профиль/chroma, ложный codecHint,
зашифрованные entries и внешние ссылки. Сохраняются проверки диапазонов,
отмены, init rewrite и reader lifetime. ASan/UBSan инструктирует этот общий C++
путь; внешние Qt/libmpv/DirectShow библиотеки в этом sanitizer-прогоне не участвуют.

Отдельный FragmentIndex-тест сравнивает каждый пакет с FFprobe с допуском 1 мкс
для PTS/DTS, включая B-frames, и подтверждает отказ HEVC без явного допуска v2.

## Сборки, установка и ограничения

GCC Linux, MinGW Qt Windows и MSVC Classic: итоговые сборки exit 0.
Classic пересобран вместе с DSUtil, BaseSplitter и MP4Splitter.
Предыдущие неуспешные диагностические прогоны не выдаются за PASS: первый Linux
focus имел exit 8 из-за общего каталога параметризованных тестов, первая MSVC
сборка — exit 1 из-за RapidJSON overload, первый Classic regression — exit 1
из-за прежнего codec guard. Они исправлены и заменены итоговыми прогонами выше.

Установленный Linux EXE заменён атомарно после успешного regression. Прежний EXE
сохранён для отката; INI сохранил SHA-256
`ac9387b8d09f63317ed8ea3b681abad96e5abe8890400d4db6540017e3acde75`.
Версия продукта остаётся 0.2.0; новый медиапрофиль определяется capabilities и
хешем сборки, SDK имеет отдельный номер 1.14.

Windows установки и профили не заменялись. Classic тестируется в отдельной копии
с ContextMenuPrompted=1 и ContextMenuEnabled=0; HKCU команды/иконки до/после
совпали. Одноразовая интерактивная задача удалена, LastTaskResult=0.

Фикстуры — локальные 320×180, 25 fps, 20 секунд. Не подтверждены 4K realtime,
HDR passthrough, физическая A/V-синхронизация, 30-минутный дрейф, дополнительные
контейнеры/кодеки, remux/live, реальный Harvester, оборудование или Wayland.
