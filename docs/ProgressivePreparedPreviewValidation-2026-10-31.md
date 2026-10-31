# SDK 1.53: проверка bounded RAM MP4 preview

Контракт: [ProgressivePreparedPreview.md](ProgressivePreparedPreview.md).
Машинный отчёт с SHA-256 исходников/EXE, отдельными exit codes и статусами:
[JSON](ProgressivePreparedPreviewValidation-2026-10-31.json).
Harvester не менялся. Результат относится к одному подготовленному фрагменту;
непрерывный progressive-remux и полное ТЗ этим отчётом не закрываются.

## Исполненные проверки

| Редакция | Новый режим | Доказательство декодирования | Регрессии | Exit code |
|---|---|---|---|---|
| Linux Qt, Qt 6.4.2/libmpv | 20 checks, 4 профиля | Меняющиеся хеши кадров framebuffer; audio-pts | 14 CTest groups | 0 |
| Windows Qt, Qt 6.12.0/libmpv | 20 checks, 4 профиля | Меняющиеся хеши кадров framebuffer; audio-pts | 14 CTest groups | 0 |
| Windows Classic, MFC/DirectShow | 20 checks, 4 профиля; отдельная проверка истории | Native loaded/running/paused graph, position/duration, decoder RAM reads, естественный EC_COMPLETE | 4 muxed MP4 playback rows и v1 sessionDirectory | 0 |

Четыре профиля: H.264 video-only, H.264/AAC-LC, AAC-LC audio-only и
неполный H.264/AAC-LC исходник. Последний завершает bounded clip при
`sourceComplete=false`, сохраняя различие с настоящим EOF загрузки.
Исходные опубликованные байты проверяются на неизменность.

Проверены pause/seek/resume, запрет неявной замены, повтор того же start без
второго фрагмента, отказ при изменении запроса с прежним токеном, восстановление
потерянного start-ответа через read-only query по requestId, отказ чужим IDs.
После `ResourceProbe.forget()` уже запущенный decoder продолжает читать свою
RAM-копию; файловые reader receipts подготовки подтверждены отдельно.
Восемь последовательных immediate start/cancel завершаются readerCount=0.
Это короткий lifecycle smoke, а не 100-цикловая/многочасовая приёмка.

Qt в дополнительном тесте удаляет Player с играющим RAM-фрагментом и задерживает
native retirement управляемым test gate. UI продолжает работать; callback factory
живёт до native destroy, данные освобождаются после него. Linux и Windows: по
8 QtTest cases, exit 0. Native buffer/protocol test исполнен на обеих ОС;
Linux дополнительно собран и запущен с AddressSanitizer/UndefinedBehaviorSanitizer,
оба exit 0. SDK helper: 9 unit tests, exit 0.

14 CTest groups включают новый preview/native/SDK, прежние backend diagnostics и
remux API/native/worker/process, resource/range integrity, retirement и два
прежних shared fMP4/Opus playback-профиля. В Classic отдельно проверены
shared H.264/AAC MP4 варианты и старый v1 API; Qt-результат за Classic не выдаётся.

Qt проверяет отсутствие внутренних URI в истории. В Classic отдельно исключены
пути OpenSessionInfo, SaveHistory и AddRecent; runtime проверяет
`history.mpc_lst`. Название фрагмента передаётся отдельно от внутреннего URI.

## Изоляция и установка

Linux GUI/audio запускались через `qt/tests/run_tests.sh` с отдельными
X11/D-Bus/PulseAudio. В Windows Qt/Classic использовались частные профили,
application mute; системный микшер не менялся. Тестовый Classic до первого
старта получил ContextMenuPrompted=1, ContextMenuEnabled=0. Shell registration
HKCU проверена до/после каждого запуска. Одноразовая задача и staged-копия
удалены, tested/built EXE SHA-256 совпадают.

Установленный Linux EXE заменён атомарно с backup предыдущего SDK 1.52.
INI SHA-256 до/после:
`703b669653acb3f84eb6a1a04119733368cd2a750b2c65a1bf4e4b4b8fccfd68`.
213 остальных файлов проверены побайтно по SHA-256, worker и integration-файлы
сохранены. SDK 1.53 поставляется отдельным ZIP. При замене процессы просмотра
не останавливались. CLI version и capabilities установленного EXE сверены
с проверенной сборкой через отдельный временный профиль.
Установленные Windows EXE/INI не заменялись; их hashes дополнительно сверены
в фазе подготовки публикации.

## Практические ограничения

Срез входа прежний: MP4/M4A/MOV, H.264 и/или AAC-LC, не более 128 входных
пакетов и 16 MiB RAM-выхода. Фрагмент не расширяется по мере скачивания.
Физический A/V drift и соответствие численным порогам ТЗ не измерялись;
Classic меняющиеся pixels/физический аудиовыход этим тестом не аттестуются.
Обычный Classic graph teardown остаётся на STA. Live, расширенные codecs,
общий непрерывный remux и реальный Harvester — следующие отдельные этапы
[чеклиста](ProgressiveCompletionChecklist.md).
