# Windows Qt: точный длинный TEMP

8 ноября 2026. Приложение 0.2.0 / SDK 1.84. Закрыт ограниченный Windows Qt
runtime-path срез: TMP240/320/1100 действительно использован полным плеером,
QSettings и декодером. Общее progressive ТЗ остаётся открытым.
[Машинный отчёт](ProgressiveQtExactTempPathsValidation-2026-11-08.json).

## Изменение частной QtCore

Прежняя private QtCore сохраняла atomic INI sync и исправляла lock cleanup,
но принимала короткий OS fallback при длинном TMP. Строгий контроль той DLL
снова завершился native exit 1 на TEMP320. Это иной результат, чем ранее
объявленный PASS fallback-сценариев; старые отчёты и SHA не изменялись.

Расширен [закреплённый Qt 6.12.0 patch](../tools/runtime/qt-6.12.0-long-lock-paths.patch):
для обычного пользователя QFileSystemEngine::tempPath читает переменные в
порядке TMP → TEMP → USERPROFILE, динамически нормализует выбранный путь
и сохраняет его, если длина >=MAX_PATH. Короткая выбранная папка оставляет
прежний native путь; следующий длинный TEMP не перекрывает короткий TMP.
Пустые переменные пропускаются, относительное имя раскрывается через
GetFullPathNameW. Буферы и повтор при изменении environment ограничены.

LocalSystem и случай, когда token нельзя проверить, оставлены штатному
GetTempPath2W: его отдельная SystemTemp политика не заменяется пользовательским
TMP. Реальный SYSTEM-процесс этим стендом не проверялся; это явно сохранённая
ветвь кода, а не подтверждение SYSTEM qualification.
Основание: [GetTempPath2W: non-SYSTEM precedence и SystemTemp](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-gettemppath2w).

QT-код приложения, IPC, endpoints, CLI, INI/defaults и libmpv не менялись.
Изменена только Windows QtCore DLL и тесты/инструкция её сборки. Глобальная
MSYS2 QtCore не заменяется. Это другой механизм, чем process-local Detours
[Classic](ProgressiveClassicLongTempPlayback.md); редакции остаются независимыми.
Linux Qt не затронут этим Windows source patch.

## Финальные проверки

| Проверка | Подтверждённый результат |
|---|---|
| Full Qt main/legacy CLI | 6 случаев: profiles0/320/1100, exact TEMP240/320/1100, каждый normal main exit 0 |
| Реальные runtime files | Broker locks313/393/1173 и ProfileLease338/418/1198 chars внутри запрошенных TEMP roots; удаления проверены после выхода main |
| Strict QSettings TEMP | 3 контекста ×3 строки, native exit 0; ordinary/320/1100 INI, повторные atomic sync, отсутствие orphan lock, independent reader, concurrent merge и сохранённое значение |
| QSettings обычный TEMP | 3 рабочие строки + init/cleanup = QtTest5 PASS, exit 0 |
| Player decoder под long TEMP | 3 контекста ×5 строк =15 рабочих случаев; каждый QtTest7 включая init/cleanup, native exit 0 |
| Windows port regression | 8 рабочих строк + init/cleanup =10 PASS, native exit 0 |
| Legacy adapter обычный TEMP | 5 строк + init/cleanup =7 PASS, native exit 0 |
| Приоритеты каталогов | 4 native probe случая: short TMP wins, empty TMP→long TEMP, long USERPROFILE, relative long TMP, exit 0 |
| DLL ABI/config | x86_64, 6920 stock exports сохранены, все 216 feature flags совпали |
| Private deploy | exit 0, standalone --version 0.2.0; DLL/EXE/winpthreads SHA совпали с проверенным stage; старый patch pin отклонён до создания Destination |

Full main проверил IPC buffering/playing/paused, pause/resume, append/complete,
явный provider release при живом main, malformed-session stderr, normal exit
через отдельный ordinary WAV и сохранность INI sentinel. Он сам не измеряет
decoder position: это отдельно покрывает Player test.

Player test выполняет существующие complete/failed/long-manifest320/1100/
provider-restart сценарии. Они ждут продвижения decoder time до завершения
загрузки и проверяют release. Используются изолированные Qt profiles,
software output и mute; настройки микшера/мониторов/рабочего сеанса не меняются.
Это не новое измерение physical A/V или аппаратного декодирования.
Наличие QTest7 в каждом логе включает две служебные строки, а не семь разных
playback profiles. Offscreen sizeHints и trailing wakeUp warnings сохранены;
их native exits 0 не объявлены исправлением этих предупреждений.

## Сохранённые отказы и воспроизводимость

Первая DLL-кандидат прошла long-TMP playback и QSettings, но дополнительный
test empty TMP→long TEMP завершился native exit 1. Windows возвращает для
пустой переменной capacity=1, поэтому первоначальная проверка нестабильного
environment ошибочно уходила в native fallback. Условие исправлено на >1;
окончательная DLL пересобрана и вся принятая матрица повторена на ней.
Первый кандидат и его результаты сохранены отдельно. SYSTEM не запускался.

Source archive Qt6.12.0, 11 MSYS2 patches и CppWinRT pin остались прежними.
Финальный overlay подготовлен повторно из свежего каталога и совпал с pin:
`1a629a01bedac34da87d91440ae4f2af96ece63674d919ec99a5edaa9d9d539b`.
Это source-overlay reproducibility, не обещание побайтового повторения линкера.
Core target rebuild и validation — native exits 0. Финальная QtCore SHA256:
`945da33a0da53fd11cdaa20996f03c1503226d44039e8ae3c961bae26c2e419b`.
[Сборка/упаковка](../qt/packaging/windows/README.ru.md) требуют текущий patch pin;
не следует брать metadata/DLL раннего кандидата из прежнего build directory.
Лицензии и точные source pin/patch/tools включены в private deployment.
Опубликованные 0.2.0 assets не заменялись и этой DLL автоматически не получили.

## Изоляция и границы

Все 583 protected SHA и 158 HKCU entries совпали с исходным checkpoint;
для shell использован последний проверенный Classic snapshot предыдущего этапа.
Три one-shot task удалены, соответствующих процессов не осталось. Частные
корни main/Player/QSettings/policy удалены. Общие runtime directories Windows
не очищались произвольным sweep: старые пустые lease-файлы без доказанной
принадлежности не удалялись и не объявлялись очищенными.
Установленные EXE/INI Windows, Linux main/INI, release assets и Harvester
сохранились. Linux main уже прежний, новая установка ему не нужна.

Точный TEMP здесь означает используемый Qt temp/runtime root, подтверждённый
физическими lock/lease файлами. Не проверен каждый сторонний WinAPI consumer
в каждом DLL или во всех source/format combinations. Открыты внешний Python
launcher с длинным TEMP, полная long-root EOF/error/format/stress матрица,
physical A/V и чистые релизные сборки. Пограничные приоритеты пустых переменных
Classic следует проверить отдельно: его resolver в этом Qt-only этапе не менялся.
