# Classic: воспроизведение при длинном TEMP

8 ноября 2026. Приложение 0.2.0, SDK 1.84. Закрыт ограниченный MP4/AAC/H.264
срез Classic TEMP120/240/320/1100. Общее ТЗ этим не завершено.
[Машинный отчёт](ProgressiveClassicLongTempPlaybackValidation-2026-11-08.json).
Продолжение [исправления выбора registry](ProgressiveWindowsTempManifest.md).

## Локализация второго ожидания

После исправления registry API отвечал, но граф оставался buffering.
Снимок стеков owned тестового Classic показал:

```
CGraphThread::OnOpen → CMainFrame::OpenFile → CFGManager::RenderFile
→ CMPCVideoDecFilter::DetectVideoCard → D3D9Helper::Direct3DCreate9
→ D3D9 / графические DLL AMD → LoadLibrary → DbgHelp
→ GetTempPath2W → GetTempPathW → RtlQueryEnvironmentVariable
```

Наличие драйвера в цепочке не доказывает ошибку его реализации: точное внутреннее
состояние Windows не установлено. Стек локализует ожидание при обычном создании
графа, а не сертифицирует аппаратное декодирование, HDR или новое оборудование.
Экспортные символы системных DLL могут быть ближайшими именами с displacement;
символы Classic разрешены по точной private PDB. Неопознанные адреса сохранены.

[Инструмент снимка](../tests/windows/classic_thread_snapshot.cpp) проверяет exact
EXE path, private test prefix, живой PID и общий родительский test driver.
Перебираются только потоки этого процесса; каждый кратко приостанавливается,
получает CONTEXT и StackWalk64, затем возобновляется до разрешения имён.
Используется системная DbgHelp и локальные symbols, без symbol server.
Снимок содержит 16 потоков; ошибок захвата нет, максимальная измеренная пауза
141 ms. Отдельная финальная самопроверка подтверждает стек известного waiting
worker, нулевой исходный suspend count и успешный join после capture.
Это диагностика, которая влияет на scheduling; её длительность не latency продукта.
Метод: [GetThreadContext](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getthreadcontext),
[StackWalk64](https://learn.microsoft.com/en-us/windows/win32/api/dbghelp/nf-dbghelp-stackwalk64).

## Исправление

Classic уже использует process-local Microsoft Detours для WinAPI совместимости.
В ту же startup transaction добавлен GetTempPathW adapter. Для выбранного
каталога короче MAX_PATH вызывается прежняя native функция. Для длинного пути
[bounded resolver](../shared/progressivetemppath_win.h) из предыдущего этапа
возвращает тот же полный каталог по TMP/TEMP/USERPROFILE/Windows precedence,
[adapter](../shared/progressivetemppathhook_win.h) соблюдает размер буфера,
required-size query и завершающий NUL. Исключения не выходят через WinAPI ABI.

Таким образом вызовы GetTempPathW из decoder/graphics/helper DLL внутри Classic
не попадают в обнаруженное ожидание. На проверенной Windows GetTempPath2W
делегирует этому же вызову; это отдельно проверено native fixture.
Environment, filesystem mapping, системные DLL и Windows policy не меняются.
Нет временной подмены TMP, alias/junction, смены renderer или отключения
longPathAware. Hook действует только внутри Classic; другие приложения,
Windows Qt и Linux не модифицируются. Общие owner/ACL/reparse guards сохранены.
Короткая ветвь сохраняет native directory и buffer results; last-error при
успешном native вызове не является новым публичным контрактом.

## Проверено

| Проверка | Результат и граница |
|---|---|
| Реальный Detours adapter, aware native EXE | 4 TEMP roots120/240/320/1100, native exits 0; short native equivalence, exact long root, zero/small/exact buffer, terminator/guard, invalid null buffer и GetTempPath2 delegation |
| Classic full main / v1 IPC | 8 indexed/fragmented MP4 случаев: 4 exact TEMP roots, native driver exit 0, 4 normal player exit 0 |
| Classic legacy `/harvester-session` | Манифесты 349/1142, ≥10 s partial playback до complete, Stop/release, normal player/provider exits 0; этот regression использует обычный TEMP |
| Snapshot helper | MinGW build и финальный self-test native 0; CTest entry добавлен, отдельный CTest запуск здесь не заявляется |
| Кандидат Classic | Два изменённых MSVC объекта скомпилированы native 0, private link /DEBUG:NONE native 0; остальные objects/libs из прежней проверенной сборки |

В final full-main прогоне reader registry имеет длины 172/292/372/1152 chars.
Восьми случаям действительно соответствуют запрошенные каталоги, а не fallback.
Воспроизведение началось на неполных файлах: clock>0.2 s до complete; опубликован
остаток, принят complete, выполнен cancel, через IPC и независимый native helper
получены положительные released/filesClosed, затем forget. Исходные bytes сверены.
Registry и частные EXE/INI удалены; плееры закрылись обычным WM_CLOSE.
Это ограниченная проверка запуска и lifecycle, а не 10 s просмотра на каждом
long-TEMP root. Long-root natural EOF, writer error, остальные контейнеры,
physical A/V и общая stress/format матрица остаются открытыми.

## Сохранённые проблемы стенда

Первый snapshot-прогон с прежним Classic сохранил buffering/zero clock и exit 1.
После failure его собственник завершён ОС; process-exited receipt не подменяет
явный filesClosed при живом reader. Ранее сохранённые FAIL не переписаны.

Две private debug-link попытки завершились native LNK1103 (1103), с предупреждениями
о type records/PDB. Кандидат принят после private link /DEBUG:NONE с теми же
скомпилированными изменёнными объектами. Одна промежуточная компиляция native
fixture имела exit 2 из-за смешанных DWORD/unsigned в initializer list;
явный array<DWORD> исправил её. Финальный fixture build native 0.
Это рабочая private сборка, не clean reproducible release build. Для релиза
потребуется согласованная чистая сборка с отладочными symbols и полная квалификация.

Первый успешный 8-case отчёт вычислял privateCopiesRemoved до выхода из
TemporaryDirectory и записал false. Реальное удаление подтвердилось независимо.
Oracle исправлен и усилен assert; полный final 8-case прогон повторён с
privateCopiesRemoved=true. Старый отчёт оставлен, его false не скрыт.

## Сохранность и следующий шаг

583 protected SHA и 158 HKCU entries совпали до/после. ContextMenuPrompted=1,
ContextMenuEnabled=0 записаны до первого запуска каждой private Classic copy.
Три одноразовые задачи удалены, owned процессов и проверенных тестовых roots
не осталось. Никакой установленный Windows EXE/INI или опубликованный asset
не заменён. Linux main/INI побайтово прежние; Linux-код не изменялся, повторная
установка не нужна. Harvester и настройки рабочего сеанса сохранены.

Далее остаются точный long TEMP Windows Qt вместо системного fallback, внешний
Python launcher, расширенные long-root EOF/error/format проверки и прочие
пункты [общего чеклиста](ProgressiveCompletionChecklist.md).
