# FoxicMP — MSI и EXE для Windows x64

Установщики выпуска 0.2.0 собираются из чистых portable-папок. Для Classic и Qt
используются независимые ProductCode/UpgradeCode, ярлыки и папки приложения.
MSI устанавливает файлы, а EXE — WiX Burn-оболочка над тем же MSI. Для обычной
установки достаточно выбрать один формат своей редакции.

По умолчанию установка выполняется для текущего пользователя:

- Classic: `%LOCALAPPDATA%\Programs\FoxicMPClassic`;
- Qt: `%LOCALAPPDATA%\Programs\FoxiqMPqt`.

Папка доступна для записи: настройки и личные файлы остаются рядом с EXE.
Расположение можно выбрать в MSI-интерфейсе; для автоматической установки:

```powershell
msiexec /i FoxicMPClassic-0.2.0-windows-x64.msi INSTALLDIR="C:\MyApps\FoxicMP" /qn /norestart
.\FoxicMPQt-0.2.0-windows-x64-setup.exe /quiet /norestart InstallFolder="C:\MyApps\FoxiqMP"
```

Путь сохраняется отдельно в служебной записи Windows Installer и восстанавливается
при repair. Эта запись — метаданные установки, не профиль плеера. Установщики
не регистрируют плеер как основной, не меняют контекстные команды, не запускают
приложение автоматически и не переносят настройки из других папок.

Classic получает заводской INI только если его ещё нет. Его компонент помечен
`NeverOverwrite` и `Permanent`; существующий INI сохраняется при repair/upgrade
и uninstall. Qt создаёт INI самостоятельно, установщик им не управляет.
Пользовательские файлы удаление приложения не очищает.

## Сборка

Нужны Python 3 и WiX Toolset 3.14 с WixUIExtension/WixBalExtension. На машине
с общей исходной папкой генератор можно запустить из Linux, а линкер — в Windows:

```sh
python3 packaging/windows/generate.py --edition classic --version 0.2.0 \
  --stage /path/to/clean/Classic \
  --windows-stage E:/release/Classic --windows-repo E:/src/FoxicMP \
  --windows-output E:/release/installers --output /path/to/shared/installers
```

Для Qt замените `--edition classic` на `--edition qt` и пути к clean-папке.
Classic INI проверяется против `distrib/Defaults/FoxicMP.ini`; личные Qt INI
отклоняются. Лицензии в папке `licenses` сохраняются, включая `COPYING.LIB`.

```powershell
./packaging/windows/build.ps1 -ProjectDirectory E:\release\installers
./packaging/windows/test.ps1 -Edition qt -ProjectDirectory E:\release\installers -TestRoot C:\Temp\FoxicMP-test
```

Тест требует новый TestRoot и отсутствие установленного MSI-продукта этой
редакции. Он использует отдельную папку и проверяет MSI install/repair/uninstall,
EXE install/uninstall, сохранение заранее созданного INI и неизменность команд
Classic в HKCU. `-InstalledProfiles` позволяет дополнительно передать пути
профилей установленных приложений для проверки SHA-256 до/после.

Проверки MSI ICE64 проходят: при удалении убираются только пустые каталоги.
ICE38 исключён из-за сохраняемого INI с файловым keypath; ICE91 исключён,
поскольку пакеты предназначены только для одного пользователя, без ALLUSERS.
Основание: [WiX Component](https://docs.firegiant.com/wix3/xsd/wix/component/),
[WiX Standard Bootstrapper](https://docs.firegiant.com/wix3/bundle/wixstdba/).
