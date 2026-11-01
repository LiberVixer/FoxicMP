# Qt: внутренний PCM-адаптер для gapless

Продолжение [PCM-эталона](ProgressiveMp3GaplessReference.md). Обрезка теперь
реализована в общем `qt/src/player.cpp` и проверена через **сам `Player`**
на Linux и Windows, а не только через отдельный libmpv probe.
[Отчёт](ProgressiveQtPcmWindowValidation-2026-11-01.json) содержит границы
проверки, хеши, exit codes и результаты PCM capture.

Это внутренний шаг. Progressive controller и Classic PCM adapter пока
не подключены; публичный gapless capability не объявляется. API/SDK 1.66
сохраняет encoded-sample timeline и `gaplessTrimApplied=false`.

## Реализация

`Player::loadPcmWindowSource(url, options, AudioPcmWindow)` принимает явное
полуоткрытое окно абсолютных decoder samples и sample rate. Метод не
угадывает encoder delay/padding и не проверяет LAME tag: будущий controller
обязан передавать доказанные границы подходящего decoder/profile.
До этого требуется настроить управляемую аудиоцепочку `setAudioGraph`.

Порядок фильтров:

1. Частный `foxicmp-pcm-window`: `asettb=1/sampleRate`, затем `atrim` по
   абсолютным `start_pts/end_pts`. Сэмплы не пересчитываются `asettb`.
2. Пользовательская цепочка `foxicmp-audio`.
3. Выходной формат/частота `foxicmp-audio-format`.

Окно не обрезается второй раз в пользовательской цепочке. Повторный
`setAudioGraph` сохраняет частный фильтр перед обработкой и преобразованием
формата. Параметры фильтров для `loadfile` передаются с quoting по числу
UTF-8 bytes, без построения строк shell-команд.

Следующий обычный `loadSource` удаляет окно и применяет последнюю управляемую
цепочку, включая пользовательские изменения во время предыдущего файла.
Это предотвращает откат цепочки из per-file backup libmpv. `stop` очищает
отложенное окно; обычный `loadfile ... replace` после такого воспроизведения
проходит через тот же путь очистки. Очередь загрузки до готовности renderer
сохраняет окно вместе с options.

Некорректное окно, неподходящий sample rate, границы выше точного целого
диапазона double и конфликтующие `af` options отклоняются до смены источника.
Настройки профиля этот адаптер не записывает.

## Проверки

По 72 проверки на ОС, восемь четырёхсекундных Xing/Info-профилей:
mono/stereo, 44100/48000 Hz. Полная подтверждённая MP4-проекция получена
через production bridge; цифровой PCM записывается из настоящего `Player`.
Проверяются точная длина и SHA-256 относительно native decode того же файла:

- обычный native MP3;
- окно с начала, первого сэмпла, середины вне MP3-кадра и последнего сэмпла;
- пользовательский `volume=0.5` вместе с trim;
- два последовательных обычных reload без переноса trim;
- stop и обычный `loadfile ... replace`;
- изменение пользовательской цепочки во время trim и сохранение изменения
  после двух reload.

Probe также отклоняет четыре неверных запроса и проверяет сохранность
списка фильтров. В probe при каждом `FILE_LOADED` цепочка применяется повторно,
поэтому проверяется реальный путь её перестройки.

Дополнительно прошли 11 QtTest rows прежней обработки звука Linux и
по восемь progressive runtime profiles каждой ОС: прежний MP3 и Xing,
с buffering/pause/seek/update/complete/EOF/release. Linux UI/audio запущены
через частные X11/D-Bus/PulseAudio; PCM probe открывает только `ao=pcm`.
Windows проверен отдельно; установленный Classic не запускался и результат
Qt не засчитывается за DirectShow.

Возвращённые subprocess/CTest codes сохранены отдельно от статуса
инструмента запуска. После успешного Linux wrapper с кодом 0 инструмент
дважды сообщил финальный статус сессии 143; повтор в отдельной process
session снова прошёл 72 проверки. Окончательный полный прогон вернул 0
и из wrapper, и из инструмента. В JSON прежние 143 не подменены нулями.

## Открытые границы

- Привязка audible duration/seek/chapters/available к coded decoder time
  в настоящем progressive controller и growing playback.
- Проверенные LAME metadata/CRC и отдельный совместимый gapless-контракт.
- Exact PCM adapter и capture выходных сэмплов Classic.
- Расширенная матрица пользовательских resamplers/complex graphs при trim;
  текущая PCM-проверка использует исходную частоту, `anull` и `volume=0.5`.
- Физический аудиовыход и отложенные аппаратные проверки.

Обычные установленные Windows EXE/INI не заменяются. Проверенная Linux
сборка обновляется атомарно с backup и сохранением текущего профиля.
Harvester не меняется. Полная gapless-функция остаётся открытой.

Для повторения собрать `foxicmp-player-pcm-probe` и
`foxicmp-progressive-media-tests`, затем выполнить CTest
`progressive-mp3-qt-pcm-adapter`. На Linux использовать
`qt/tests/run_tests.sh BUILD MEDIA -R '^progressive-mp3-qt-pcm-adapter$' -V`.
