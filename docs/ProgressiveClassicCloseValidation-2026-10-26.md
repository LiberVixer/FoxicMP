# Проверка progressive Classic close — 26 октября 2026

[SDK 1.13](ProgressiveClassicClose.md). База —
`69bbb754f3f52ca1a0d4899993ef0824e6b7aa02`.
[Машинный отчёт, хеши и замеры](ProgressiveClassicCloseValidation-2026-10-26.json).

## Финальная сборка и результаты

MSVC Release x64; DSUtil, BaseSplitter, MP4Splitter и приложение: exit **0**.
SHA-256 Classic:
`18e6495f05cc5cc3d52e36a9e659bb087fc9d507ef9c46989028554944ffc044`.
Хеш отдельной проверенной копии совпадает с итоговым EXE.

| Проверка | Результат |
|---|---|
| Новый async-close стенд | 5 сценариев PASS |
| SDK/reader/resource/v2/preparation/publication/registry | 8 групп контрактов, exit 0 |
| Legacy/provider playback | Просмотр до complete, затем complete и release; exit 0 |
| Legacy maintenance | Grow, Stop, unchanged truncate: 3 PASS |
| Legacy opening | Commit, Close, Stop, deadline: 4 PASS |
| Весь Classic runner | exit 0 |
| Python SDK | 35 тестов, exit 0 |
| Общий family contract | 2 теста, exit 0 |

## Что действительно задерживает новый стенд

`tests/windows/progressive_classic_close_test.py` находит именованный STA только
в своём тестовом процессе и временно приостанавливает его. Это реальный графовый
поток, без подмены close event или декодера. В finally он обязательно возобновляется.

Проверены:

- Stop после реального начала воспроизведения и Stop до выполнения открытия в STA.
- Принятие нового start с buffering/position=0, затем его запуск после старого close.
- После Cancel допускается обычный start без replace; конкурирующий start без
  replace при уже активном pending-сеансе получает session_busy.
- Замена ожидающего start ещё одним, Stop и отсутствие позднего запуска обоих.
- Выход через posted WM_CLOSE: процесс/окно живы и отвечают, пока STA задержан;
  после возобновления teardown и выход завершаются с кодом 0.
- Reader старого графа не released при задержке. После реального закрытия файлы
  освобождаются; отменённые ожидающие источники также подтверждают release.

В пяти контролируемых случаях ответ cancel занял менее **1,4 мс**, максимальный
WM_NULL ping — **0,3 мс**. В тесте для cancel допускается <1 с, для UI ping — 500 мс.
Это замеры управляемой задержки STA, не обещание такого времени на любом драйвере,
файловой системе или при создании/закрытии любого окна плеера.

Первый прогон обнаружил лишнюю strong reference в m_lastOMD. Prepared Source
теперь передаётся из OpenFileData текущему графу при активации. Финальные пять
случаев и вся регрессия выполнены после этого исправления и защиты deferred open
от нового Stop/Close. Проверка не ограничена только немедленным IPC-ответом.

## Изоляция и границы

Отдельная Windows-копия с UTF-16 INI: ContextMenuPrompted=1,
ContextMenuEnabled=0. Команды/иконки установленного Classic в HKCU до/после
совпали. Одноразовая интерактивная задача удалена; LastTaskResult=0.
Установленные Windows EXE/профили не заменялись, Harvester не изменялся.

Qt-код не менялся и заново не квалифицировался; результаты Qt SDK 1.12 остаются
в [предыдущем отчёте](ProgressiveEngineShutdownValidation-2026-10-26.md).
Linux EXE и INI сохранили прежние хеши. Обновляется только integration SDK.

Асинхронный путь проверен для progressive-графов в штатном STA. Обычные источники,
UI-owned графы, GL free, аппаратный звук, диски и Wayland этим этапом не закрыты.
Проверки из настоящего YouTubeHarvester не было.
