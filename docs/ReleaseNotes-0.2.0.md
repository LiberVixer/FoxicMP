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


## Установщики — дополнение 0.2.0-installers.1

Для каждой Windows-редакции добавлены самостоятельный MSI и EXE, содержащий тот
же MSI. Установка по умолчанию — в доступную для записи пользовательскую папку
программы, с отдельным ярлыком. Проверяются install, repair и uninstall, включая
сохранение ранее созданного INI и выбранного пути установки. Автоматической
регистрации контекстного меню и запуска плеера нет.

Для Linux Mint 22.x добавлен `foxiqmp-qt_0.2.0-1_amd64.deb`. Системный payload
не содержит пользовательских данных; при запуске создаётся переносимая копия
в `~/.local/opt/FoxiqMP-qt`. INI и история остаются рядом с программой и сохраняются
при обновлении/удалении пакета. Зависимости вычислены по ELF через dpkg-shlibdeps.

Исправлен заголовок обеих Qt-редакций: он использует версию QApplication вместо
устаревшей строки 0.1.73. Linux-интерфейс переснят с настоящим оконным заголовком,
верными пропорциями и без курсора поверх логотипа. Исходники дополнения закреплены
тегом `v0.2.0-installers.1`; исходный тег portable-выпуска `v0.2.0` сохранён.

[Windows-установщики и тест](../packaging/windows/README.md) ·
[DEB: установка, профиль и сборка](../qt/packaging/debian/README.md).

| Редакция | EXE | MSI / DEB |
| --- | --- | --- |
| Windows Classic | [Установщик EXE](https://github.com/LiberVixer/FoxicMP/releases/download/v0.2.0/FoxicMPClassic-0.2.0-windows-x64-setup.exe) | [MSI x64](https://github.com/LiberVixer/FoxicMP/releases/download/v0.2.0/FoxicMPClassic-0.2.0-windows-x64.msi) |
| Windows Qt | [Установщик EXE](https://github.com/LiberVixer/FoxicMP/releases/download/v0.2.0/FoxicMPQt-0.2.0-windows-x64-setup.exe) | [MSI x64](https://github.com/LiberVixer/FoxicMP/releases/download/v0.2.0/FoxicMPQt-0.2.0-windows-x64.msi) |
| Linux Qt | — | [DEB amd64](https://github.com/LiberVixer/FoxicMP/releases/download/v0.2.0/foxiqmp-qt_0.2.0-1_amd64.deb) |

[SHA-256 установщиков](https://github.com/LiberVixer/FoxicMP/releases/download/v0.2.0/SHA256SUMS-installers.txt).
MSI и EXE каждой редакции прошли install/repair/uninstall в отдельной тестовой
папке, с сохранением предварительно изменённого INI и неизменностью рабочих
профилей и контекстных команд. Все операции завершились с кодом 0.
DEB: сборка и apt-симуляция — exit 0; запуск распакованного launcher в отдельном
HOME — exit 0; тесты обновления/сохранения профиля — 3/3. Установка DEB через
системный dpkg на рабочей машине в эти проверки не входила.

![Исправленный Linux-интерфейс 0.2.0](https://raw.githubusercontent.com/LiberVixer/FoxicMP/v0.2.0-installers.1/docs/images/foxicmp-qt-linux-0.2.png)
