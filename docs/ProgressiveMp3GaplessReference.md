# MP3 gapless: PCM-эталон следующего адаптера

Продолжение [SDK 1.66](ProgressiveMp3Indexed.md), без изменения версии
приложения, API или установленного воспроизведения. Gapless плеера пока
не готов. [Результаты](ProgressiveMp3GaplessReferenceValidation-2026-11-01.json)
содержат exit codes, версии движков, границы и SHA-256 PCM каждого сценария.

## Проверенный результат

По 128 проверок Linux/Windows для восьми настоящих четырёхсекундных
Xing/Info-профилей: mono/stereo, 44100/48000 Hz, VBR/CBR. VBRI исключён:
его контракт не предоставляет LAME encoder delay/padding.

Стенд читает виртуальный MP4 через decoder API **production bridge**,
а не собирает альтернативную проекцию. Далее проверяются:

- native MP3 decode и полный decode проекции, точное число сэмплов и PCM;
- независимость окна от порций: по одному сэмплу, по 1152 и неравномерно;
- шесть seek targets FFmpeg: начало, первый сэмпл, 1151-й, середина вне
  границы кадра, последний сэмпл и точный конец; restart с 250 ms preroll;
- настоящий PCM file output libmpv: native MP3 и виртуальный MP4;
- `lavfi atrim` внутри libmpv по абсолютным `start_pts/end_pts`, четыре
  целевые позиции, включая середину и последний сэмпл;
- отказ от восьми непригодных наборов параметров окна;
- сохранность оригинала и освобождение reader после чтения проекции.

PCM сравнивается с native decode **того же файла и движка**. Fixtures
отдельно кодируются на двух ОС: одинаковое имя профиля не гарантирует
одинаковые encoded bytes. Отчёт сохраняет SHA-256 fixture; межплатформенное
равенство PCM требуется только для одинаковых fixtures.

libmpv использует только `ao=pcm` и частный временный файл, не открывает
звуковое устройство, профиль пользователя или окно. Проверены Linux libmpv
0.37.0 / FFmpeg 6.1.1 и Windows libmpv 0.41.0 / FFmpeg 9.0.2. Это цифровой
выход движка Qt, не физический аудиовыход. DirectShow/Classic не измеряется.
Порции PCM моделируют границы update, но не заменяют growing playback test.

## Сэмплы и временная шкала

Для проверенного FFmpeg MP3 decoder delay `D=529` задаётся явно; это не
константа любого MP3 decoder. При `N=declaredFrames*1152`, encoder delay `E`
и padding `P` проверенное полуоткрытое окно PCM:

```text
begin = E + D
end   = N - P + D
audibleSamples = end - begin = N - E - P
```

Эталон требует `P >= D`, `E/P <= 4095` и `E+P < N`. Другие значения
отклоняются как не доказанные. Отсутствующие metadata нельзя заменять
нулями. LAME tag CRC остаётся отдельной незавершённой проверкой.

Для audible seek sample `T` декодирование начинается с preroll до
`begin+T`, PCM выпускается из `[begin+T,end)`. Абсолютная позиция сохраняется
между порциями. Сброс счётчика на update, повторное применение delay после
seek и обрезка целыми MP3-кадрами этому эталону не соответствуют.

Проверка фильтра libmpv декодирует раньше цели и применяет абсолютные
границы `atrim`. Она доказывает пригодность механизма для этих файлов;
подключение к progressive controller ещё не выполнено.

## Следующие шаги трёх редакций

1. Отдельный явный gapless-контракт с audible duration и доказанными
   delay/padding; сохранить семантику `standalone-mp3-indexed` SDK 1.66.
2. Qt: file-local фильтр с сохранением пользовательских audio filters.
   Согласовать audible seek/clock/duration/chapters с coded decoder time
   и доступной границей. Снимать фильтр на stop/replacement; исключить
   повторную обрезку native decoder.
3. Classic: точный PCM/output adapter и передача границ через проверенный
   graph. Текущий `CMpaDecFilter::Deliver` отбрасывает целый отрицательный
   блок; это не доказывает обрезку до отдельного сэмпла. Внешний decoder без
   подтверждённого trim не должен получать capability.
4. Цифровой PCM capture в фактических Qt/Classic playback paths, затем
   growing wait/update/pause/complete/release, пользовательские audio
   filters и обычные завершённые файлы.

Gapless остаётся открытым до этих проверок. Ни один checkbox готовой
функции в [чеклисте](ProgressiveCompletionChecklist.md) не закрывается.
Harvester и установленные профили не меняются.

## Повторение

В настроенном Qt build tree каждой ОС:

```sh
cmake --build BUILD --target foxicmp-progressive-media-tests foxicmp-mpv-pcm-probe
ctest --test-dir BUILD -R '^progressive-mp3-gapless-reference$' -V
```

Нужны Python, FFmpeg/ffprobe с libmp3lame и библиотека libmpv Qt build.
Отсутствие dependency или несовпадение PCM — ошибка, не SKIP.
Отдельный JSON-отчёт:

```sh
python tests/progressive_mp3_gapless_reference.py \
  --runner BUILD/foxicmp-progressive-media-tests \
  --mpv-runner BUILD/foxicmp-mpv-pcm-probe \
  --output gapless-reference.json
```

На Windows executable paths имеют суффикс `.exe`.

Первичные источники: [FFmpeg MP3 demuxer](https://github.com/FFmpeg/FFmpeg/blob/master/libavformat/mp3dec.c),
[libmpv client API](https://github.com/mpv-player/mpv/blob/master/include/mpv/client.h),
[mpv audio filters](https://github.com/mpv-player/mpv/blob/master/DOCS/man/af.rst).
