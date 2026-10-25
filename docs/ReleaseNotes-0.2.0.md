# FoxicMP 0.2.0

Релиз объединяет текущий набор возможностей трёх самостоятельных редакций:

- **FoxicMP Classic** для Windows сохраняет собственный профиль, внешний вид,
  штатные страницы настроек и отдельную регистрацию контекстного меню. Порядок
  верхнего меню и дерева настроек приведён к общей семейной схеме без замены
  Classic-страниц Qt-элементами.
- **FoxiqMP qt** для Windows получил тот же набор команд панели и контекстных
  меню, обработку медиаклавиш Windows, воспроизведение растущих файлов через
  progressive API и второй выход WASAPI для зеркалирования звука.
- **FoxiqMP qt** для Linux использует тот же Qt-код, progressive API через Unix
  socket, общие команды, плейлист, историю, избранное, главы и автономный
  профиль рядом с программой.

В репозитории опубликованы машинно-читаемые контракты семейства:

- `docs/FoxicMPFamilyCommands.json` — каталог общих команд;
- `docs/FoxicMPFamilyCapabilities.json` — матрица возможностей и границ
  платформ;
- `tests/family_contract_test.py` — проверка согласованности каталога и трёх
  редакций.

## Проверки

Перед выпуском выполнены:

- сборка Linux Qt и обновление установленного `_bin/FoxicMPQt-linux-x64` до
  0.2.0 с сохранением пользовательского INI;
- сборка Windows Qt и тесты progressive API (11/11), панели управления (3/3)
  и аудиозеркалирования (3 пройдено, 2 пропущено из-за отсутствия
  детерминированного WASAPI-устройства в тестовой среде);
- сборка Windows Classic после изменения порядка меню;
- семейный контрактный тест и проверка Python-инструментов;
- проверка `git diff --check` с сохранением CRLF-формата Windows-исходников.

Аппаратный звук на конкретных устройствах и интерактивное воспроизведение из
YouTubeHarvester требуют отдельной проверки в пользовательской среде. Сам
YouTubeHarvester в этом релизе не изменяется.

Профили трёх редакций, EXE и системная регистрация остаются независимыми.


## Скачать готовую сборку x64

| Редакция | Пакет | Запуск |
| --- | --- | --- |
| Windows Classic | [FoxicMPClassic-0.2.0-windows-x64.zip](https://github.com/LiberVixer/FoxicMP/releases/download/v0.2.0/FoxicMPClassic-0.2.0-windows-x64.zip) | `FoxicMP64.exe` |
| Windows Qt | [FoxicMPQt-0.2.0-windows-x64.zip](https://github.com/LiberVixer/FoxicMP/releases/download/v0.2.0/FoxicMPQt-0.2.0-windows-x64.zip) | `FoxicMPq.exe` |
| Linux Qt | [FoxicMPQt-0.2.0-linux-x64.tar.gz](https://github.com/LiberVixer/FoxicMP/releases/download/v0.2.0/FoxicMPQt-0.2.0-linux-x64.tar.gz) | `./foxicmp-qt` |

Архивы portable: распакуйте нужный пакет в отдельную доступную для записи папку.
Windows Qt включает Qt/libmpv, FFmpeg, yt-dlp и лицензии зависимостей.
Classic включает заводской INI, языковые DLL, шейдеры и необходимые DirectX DLL.
**При обновлении Classic сохраните свой `FoxicMP64.ini`: файл из архива содержит
заводские настройки для новой установки.**

Внутренняя версия движка Classic сохранена как 1.9.1.75; 0.2.0 — общий выпуск
семейства. Qt показывает 0.2.0 в `--version`.

Linux Qt рассчитан на Linux Mint 22.x x64 и использует системные библиотеки:

```sh
sudo apt install libmpv2 liblcms2-2 libqt6dbus6 libqt6widgets6 libqt6openglwidgets6 qt6-qpa-plugins mint-meta-codecs ffmpeg yt-dlp
```

Для архивов опубликованы `.sha256` с именами файлов без локальных путей.
`release-manifest.json` содержит исходный коммит, версии компиляторов и результаты
проверок. На свежей сборке полный Windows Qt тест в интерактивном сеансе:
**22 passed, 0 failed, exit 0**. Progressive API и audio-mirror на Windows и Linux:
по **2/2 набора тестов, exit 0**. Сборки всех трёх редакций: **exit 0**.
