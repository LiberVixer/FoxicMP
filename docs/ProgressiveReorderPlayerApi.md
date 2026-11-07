# B-frames: внутренний controller/Player/API срез

5 ноября 2026. Приложение 0.2.0, SDK 1.82. Продолжение
[managed decoder этапа](ProgressiveManagedReorder.md).
[Первичные результаты и SHA](ProgressiveReorderPlayerApiValidation-2026-11-05.json).

## Изменение

`continuousRemuxStart` принимает дополнительный внутренний параметр
`mode=continuous-h264-reordered-video-v1`. Без него сохраняется прежний
`continuous-h264-video-v1`, который отвергает B-frames. Это внутреннее расширение
для приёмки; capability, worker info и поставленный Python SDK пока объявляют
только прежний профиль. Новый режим ещё не является публичным SDK-контрактом.

Профиль хранится в задании controller, передаётся в managed parent/cache и
неизменяемый decoder source. Qt и Classic связывают playback ticket с mode
именно полученного source. Job/playback status и диагностический mediaMode
Classic отражают выбранный профиль, включая закрытый ticket после создания
следующего задания. `continuousRemuxPlay` самостоятельно режим не выбирает.

Mode входит в fingerprint исходного запроса: повтор с тем же requestId и другим
режимом — `continuous-conflict`. В update mode запрещён; неизвестное поле даёт
штатный `invalid_snapshot`. Неизвестное имя режима при start — `invalid_request`.
Ни повтор, ни поздний запрос не меняют профиль или владельца текущего видео.

## Проверки

Обычные endpoints настоящих Linux Qt, Windows Qt и Classic. Для каждой редакции
проверены 17 B-frame H.264 25 fps сценариев и один сценарий возвращения к default:

- Подготовка, явный Play, replay/request identity и изоляция endpoints.
- Pause/append/complete, completed seek и подтверждённый EOF.
- Обычный toolbar Stop и шесть Stop во время открытия без позднего restart.
- ReaderRelease при playing, pause и у непубликованного хвоста; filesClosed.
- Замена обычным видео, изоляция старых controls, сохранение просмотра после
  неудачной подготовки замены.
- Producer Cancel/Forget и required-track failure закрывают свой decoder.
- Not-ready Play не превращается в отложенный неявный запуск.
- Следующее за reorder задание без mode по-прежнему отклоняет тот же B-frame
  input; старый закрытый playback ticket сохраняет собственный mode.

Проверяется mode всех успешных continuous-ответов; прежний порог каждого
измеренного API request — <2 s. Каждая редакция также повторяет 17 прежних
no-B playback и пять SDK-сценариев. Новое CTest имя —
`progressive-continuous-reordered-playback`, timeout 300 s. Это вариант общего
`progressive_continuous_playback_test.py --reordered-video`, без копии стенда.

Qt обеих ОС дополнительно проходит семь CTest-групп: новые/прежние IPC playback,
SDK, native controller, preparation API, настоящий Player/Window и retirement.
Classic использует приватную MFC-сборку, текущие API/HarvesterSession units и
прежние проверенные renderer/static objects. Установленный Classic не запускается
и не заменяется. Реальные pixel/packet сравнения всех четырёх B-frame профилей
описаны в предыдущем native decoder отчёте; текущий UI/API срез — H.264 25 fps.

## Исходные отказы и границы

Старый Linux EXE штатно отверг новое поле mode: baseline CTest exit 8. В первом
новом тесте ошибочно ожидался `invalid_request` вместо штатного
`invalid_snapshot` для mode в update. Исправлено ожидание и cleanup scope теста;
без изменения production error codes. Остаток пустого failed cache сохранён
как свидетельство после проверки мёртвого owner, directory identity и
эксклюзивного service lock, затем убран из runtime directory.

В первой Windows Qt регрессии прежний `windowClose:reader-release` один раз
не подтвердил receipt за штатные 5000 ms. Добавлена только диагностика полного
receipt/job/playback при отказе; порог не увеличен. Три целевых повтора и полный
повтор проходят, но **причина первого отказа остаётся OPEN**. Новые и прежние
scenarios проходят отдельно; эти повторы не доказывают объяснение старого FAIL.
Исторические Linux >2 s и Classic heartbeat-12 outliers также остаются открытыми.

Linux проверки используют только частные X11/D-Bus/PulseAudio через
`qt/tests/run_tests.sh`. Classic копии до первого запуска имеют INI
ContextMenuPrompted=1/ContextMenuEnabled=0; «Форматы» не открываются. Команды и
иконки установленного Classic в HKCU сверены до/после. Временная одноразовая
задача удаляется после проверки; периодических задач нет. Harvester не меняется.

После финальных проверок Linux EXE и совместимый worker заменяются атомарно
с backup обоих файлов, проверкой discovery/worker info и сохранением INI и
всех прежних личных файлов. Просмотр не останавливается. Новые binaries Windows
остаются в частных/сборочных каталогах; пользовательские установки не меняются.

Далее: публичный opt-in capability/SDK, обновлённые пакеты и воспроизведение
скачанных assets; расширенная lifecycle/stress и fractional/VFR UI матрица.
Audio/AAC trim, общий remux, многочасовой stress и физический A/V стенд открыты.
Отложенное оборудование и Wayland не проверялись.


7 ноября: [проверка повторного чтения receipt](ProgressiveReceiptObservation.md)
уточняет этот исторический отказ. Ветка reader-release ждёт квитанцию до
уничтожения окна; configured QTRY 5000 ms не является измерением закрытия.
На обеих Qt ОС старый polling воспроизводимо падает после реального proof
при последующем registry busy, текущий latched подход и полноценный GUI-тест
проходят. Исходный ответ старого случая неизвестен; его причина остаётся OPEN.
