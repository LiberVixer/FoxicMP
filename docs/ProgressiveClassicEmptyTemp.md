# Classic: пустой TMP; диагностика внешнего Python launcher

8 ноября 2026. Приложение 0.2.0 / SDK 1.84. Исправлен ограниченный Classic
empty-TMP fallback срез. Внешний Python launcher с длинным собственным TEMP
локализован, но не объявлен исправленным.
[Машинный отчёт](ProgressiveClassicEmptyTempValidation-2026-11-08.json).

## Classic

Windows GetEnvironmentVariableW возвращает capacity=1 для существующей пустой
переменной. [WindowsTemporaryDirectory](../shared/progressivetemppath_win.h)
ошибочно принимал её за нестабильное непустое значение и выбрасывал исключение.
Новый контроль прежнего native helper снова дал exit 1 с сообщением
`Temporary-directory environment is unstable` на empty TMP.

Проверка остаточного непустого значения изменена на >1. Пустой TMP теперь
позволяет выбрать TEMP, а пустые TMP и TEMP — USERPROFILE. Приоритет короткого
TMP и bounded retry сохранены. QtCore уже получила аналогичную обработку
[предыдущим этапом](ProgressiveQtExactTempPaths.md); её DLL здесь не менялась.

| Проверка | Принятый результат |
|---|---|
| Native resolver, обычный и longPathAware EXE | 19 случаев, native exit 0: roots120/240/320/1100, empty TMP, empty TMP/TEMP, short TMP priority, relative long TMP и обычные fallback |
| Реальный process-local Detours adapter | 6 случаев, native exit 0: прежние четыре roots плюс empty TMP→TEMP320/1100 |
| Classic full main, непустой TMP | 8 indexed/fragmented MP4 случаев при TEMP120/240/320/1100, driver exit 0, четыре normal main exit 0 |
| Classic full main, пустой TMP | 4 indexed/fragmented MP4 случая при TEMP320/1100, driver exit 0, два normal main exit 0 |
| Private MSVC + native MinGW | Все compile/link/build exits 0 |

Full main действительно использовал выбранные roots: native probe и
reader registry внутри TEMP совпали. Clock>0.2 s наблюдался на неполных файлах,
затем append/complete, cancel, положительные released/filesClosed через API и
независимый native helper, forget. Исходные bytes сверены. Все частные корни
и registry удалены, forced cleanup в принятых main случаях отсутствует.
Это не полная long-TEMP форматная/EOF/error/stress или physical A/V приёмка.
Короткая ветвь WinAPI adapter по-прежнему делегирует native поведение;
плеерные empty-TMP случаи здесь относятся именно к длинному TEMP.

## Python: воспроизводимый внешний барьер

[Изолированный probe](../tests/windows/python_temp_launch_probe.py) запускает
собственный Python bootstrap и короткий native child без плеера/Harvester.
Проверен установленный Python **3.11.0 x64**. Изменяется environment только
принадлежащего тесту bootstrap; environment контроллера остаётся прежним.

| TEMP запускающего Python | TEMP native child | Наблюдение |
|---|---|---|
| обычный | 120 | completed, native exit 0 |
| обычный | 320 | completed, native exit 0 |
| 320 | 320 | CreateProcess не вернулся до внешнего deadline |
| 320 | короткий | тот же внешний deadline |
| 1100 | 1100 | тот же внешний deadline |

Три снимка owned Python-процессов показывают GetTempPathW внутри Windows
CreateProcessW/CreateProcessInternalW, в цепочке экспортных символов
BasepQueryAppCompat. В Python stdout есть before-popen, но нет after-popen.
Экспортные имена могут быть ближайшими с displacement: это наблюдение стека,
а не доказательство точной внутренней причины Windows или всех её версий.
Предыдущий raw native GetTempPathW longPathAware control даёт согласованную
[локализацию](ProgressiveWindowsTempManifest.md).

Bootstrap принудительно завершался **контроллером** после записи стеков.
Его exit 1 — cleanup, а не доказанный самостоятельный crash. Collector exit 0
означает успешный сбор пяти наблюдений, не PASS трёх deadline cases.
Запуск не вернул Popen и дочерний PID; создавался ли уже внутренний process
object Windows или выполнялся ли entrypoint child, этот снимок не устанавливает.
Короткий child env этот маршрут на проверенном ПК не обошёл. Тест вообще
не запускал плеер: кодек, renderer или настройки FoxicMP здесь не участвуют. Изменение Python/Harvester/Windows или hook в чужой процесс
не выполнялось.

Snapshot helper дополнен отдельным `--owned-python-child` режимом. Сохранены
exact image path, проверка живого процесса и того же родительского test driver,
захватываются только его потоки. В трёх снимках по четыре потока, ошибок нет,
максимальная измеренная пауза потока 16 ms. Финальные helper build/self-test
exits 0; новая ветвь проверена на этих owned sibling Python children.

## Что использовать при интеграции

На этом хосте подтверждён путь, при котором запускающий процесс сохраняет
обычный TEMP, а длинный каталог передаётся только дочернему процессу:

```python
child_env = dict(os.environ)
child_env.update(TMP=str(player_temp), TEMP=str(player_temp))
process = subprocess.Popen(player_command, env=child_env)
```

Это рекомендация для будущего интегратора, не изменение Harvester. Она не
лечит процесс, у которого собственный TMP уже длинный. Не следует временно
переписывать глобальный os.environ вокруг Popen: другие потоки могут запускать
процессы в тот же момент. Один `subprocess.run(timeout=...)` также не даёт
гарантированного deadline для самого CreateProcess; об этом говорит
[документация Python](https://docs.python.org/3/library/subprocess.html).
Стенд использует внешний контроллер, owned Job Objects и ожидание выхода,
а не оставляет невидимый зависший launch-thread в приложении.

## Сохранность и границы

До каждого Classic discovery/launch задан private INI с ContextMenuPrompted=1
и ContextMenuEnabled=0. 583 protected SHA и 158 HKCU entries совпали до/после.
Одна one-shot task удалена; соответствующих процессов и доказанных private roots
не осталось. Общие runtime directories не очищались произвольным sweep.

Кандидат собран private MSVC recompile/link /DEBUG:NONE с прежними проверенными
objects/libs; это не чистая release build. Установленные Windows EXE/INI,
Linux main/INI, Python, assets и Harvester сохранены. Linux-код не менялся,
переустановка ему не нужна. API/CLI/SDK версии и endpoints прежние.

Открыты поддержка длинного собственного TEMP внешнего Python/Windows launcher,
полная форматная/lifecycle/stress матрица, physical A/V и чистая релизная
квалификация. Наблюдение одного Windows/Python окружения не заменяет их PASS.
