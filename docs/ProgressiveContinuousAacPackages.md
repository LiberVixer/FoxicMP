# SDK 1.84: обычные приложения и проверка упаковки

6 ноября 2026. [Контракт явного AAC opt-in](ProgressiveContinuousAacApi.md),
[результаты с SHA и exit codes](ProgressiveContinuousAacPackagesValidation-2026-11-06.json).

Статус: локальная проверка обычных приложений и пакетов SDK 1.84 завершена.
Исправлена гонка completed seek в Classic. Это экспериментальная поставка;
полная ТЗ-приёмка, длительный AAC stress и физическая A/V точность остаются открытыми.
Факт публикации и проверка финальных assets фиксируются отдельно.

## Что проверено

Вместо частного Qt host запущены обычные упакованные приложения 0.2.0:
Linux Qt, Windows Qt и Classic. Импорт SDK закреплён за integration проверяемого
пакета. Проверяются SHA модулей, EXE и worker, IPC, явный play, growing playback,
pause/publication, completed seek, EOF, Stop, required-audio failure,
close/filesClosed/forget и исходные SHA. Видео с AAC проверяется в шести профилях:
44,1/48 kHz, mono/stereo, fractional fps/B-frames, aligned контроль и AAC в один sample.

| Набор | Успешные сценарии |
|---|---:|
| Обычные приложения трёх редакций: AAC и два прежних video-only mode | 60 |
| Извлечённый чистый `.deb`: те же три mode | 20 |
| Распакованные Windows portable: AAC, Classic после первичного отказа | 18 |
| Установленный Linux с отдельным тестовым профилем: AAC | 9 |
| Десять полных повторов AAC исправленной обычной Classic | 90 |
| Всего успешных сценариев после исправления, включая повторы | 197 |

Финальные прогоны stage/.deb/архивов/установки (107 сценариев) закрепляют все десять SDK-модулей за содержимым пакета,
включая Windows transport. Пути плееров, runtime и CLI генерации fixtures разделены.
Ранние отчёты повторных прогонов сохраняют исходные списки импортированных модулей.

Это ограниченная матрица, не длительный stress и не физическое измерение A/V.
Linux использует run_tests.sh и частные X11/D-Bus/PulseAudio. Windows Qt —
обычный offscreen main с отдельным mute/volume=0 профилем, фактический AO WASAPI;
системный микшер не менялся. Classic — интерактивный сеанс Windows, две частные
portable-копии с ContextMenuPrompted=1, ContextMenuEnabled=0 до первого запуска
и Null Audio Renderer. Все 158 проверенных HKCU shell entries сохранены.

## Упаковка и установка

Windows Qt собран production deploy-скриптом с проверкой runtime closure и
исправленным libwinpthread из проверенного runtime. Classic содержит отдельные
EXE, DLL, factory INI, Lang и assets. Оба portable архива проверены CRC и SHA
каждого файла после извлечения. Linux `.deb` проверен после извлечения, включая
обычный Player и SDK; launcher и executable workers сохраняются.

Первый локальный `.deb` был отклонён: тестовый Python создал __pycache__ в stage.
Он не предназначен для поставки. Чистый пакет пересобран и проверен заново.
В Debian packager добавлен запрет __pycache__, `.pyc` и `.pyo`; отрицательная
проверка получает ожидаемый exit 1 и не создаёт пакет. Новые drivers запрещают
запись bytecode, включая дочерние процессы. Личные Qt INI не упаковываются.

Установленный Linux обновлён атомарно. Резервная копия предыдущих программных
файлов: `_bin/FoxicMPQt-linux-x64/rollback-sdk184-20261106T061022Z`.
Профиль до/после имеет одинаковый SHA-256
`bad5bb09fb261a148865fa5af8e3f8a96ab416b9df784ec284c9b1c36b2b3ad8`.
Прежние Python cache-файлы, затронутые ранним тестом установки, восстановлены
из резервной копии побайтно. Существующие личные файлы сохранены; просмотр не
прерывался. Установленные Windows Classic/Qt, MPC и MPC-BE не обновлялись.

Код приложений соответствует commit b4857b047c6a3206ae315fc221150d6df41b8a04.
Окончательный чистый SDK и архивы получают отдельный manifest исходников;
обновление документации в них не изменяет проверенные EXE и Python-модули.
Проверка хешей финальных assets и факт GitHub-публикации фиксируются отдельно.
Прежний опубликованный SDK 1.83 сохраняется неизменным.

## Исправление completed seek Classic

Два исходных прогона обычного пакета и один частной диагностической сборки
получили timeout: paused оставался на 0,19–0,197 s после команды seek(13.5).
Диагностика показала, что сам DirectShow тоже остался на прежней позиции.
Неуспешные прогоны сохранены в отчёте и не засчитаны как успешные повторы.

Причина — две шкалы готовности. Remux-кэш уже объявлял outputComplete=true,
а CHarvesterSession обновляет свой fragment index фоновым потоком. Старый CanSeek
видел readyEnd=11 s и без ошибки пропускал команду к 13,5 s. Через следующий tick
индекс становился полным, но команда уже была потеряна. Сам API перед этим
правильно проверял завершённость подтверждённого кэша.

CanSeek теперь принимает completed-состояние проверенного continuous-кэша при
отсутствии failure/cancel. SeekSample выбирает известный sync sample; SampleAt
и демультиплексор дожидаются последующей публикации индекса. Доступ к исходным
неопубликованным байтам не расширяется. Прежние ограничения incomplete/sparse
сценариев и API checkSeek сохранены. Qt читает continuous cache через свой
cursor и не использует этот фоновый Classic index; его код не менялся.

Для доказательства в отдельную непоставляемую сборку введена задержка индекса
800 ms. Старый guard отказал при outputComplete=true, bridgeComplete=false,
readyEnd=11 s, target=13.5 s, exit 1. Исправленный guard при тех же значениях и
побайтно том же MP4 разрешил переход; четыре сценария seek/EOF/Stop/failure/close
прошли (exit 0). Измеренные Stop/failure close — около 0,6 s, менее 2 s.
Эти четыре проверки учитываются отдельно от 197 обычных пакетных сценариев.
Окончательный EXE не содержит hooks задержки, trace или private diagnostic action.

Для воспроизведения используйте
[генератор частного источника](../tests/windows/progressive_continuous_index_lag.py):
из старой/исправленной HarvesterSession.cpp он создаёт отдельный файл только в
_bin; скомпилировать его вместо штатной TU с /I src/DSUtil. Переменные
FOXICMP_TEST_CONTINUOUS_INDEX_DELAY=800 и FOXICMP_TEST_CANSEEK_TRACE задаются
только частному тестовому процессу. Плеер проверяется обычным
[package driver](../tests/progressive_aac_package_test.py) и защищёнными INI.
Штатный source не получает fault hooks и не заменяется генератором.

## Дальнейшая работа

Открыты AAC waiting/crash/race/stress сверх этого проверенного среза, прежние
latency/resource outliers, остальная codec/container матрица и физическая A/V
точность по общему [чеклисту](ProgressiveCompletionChecklist.md).
Harvester не менялся.
