# Три версии FoxicMP — оформление 2026-10-24

Общее имя проекта и репозитория — FoxicMP.

| Приложение | Версия | Логотип | Иконка |
| --- | --- | --- | --- |
| FoxicMP Classic, Windows | Движок 1.9.1.75 | FoxicMP / Windows Classic / #LiberVixer | Исходная оранжево-синяя |
| FoxiqMP qt, Windows | 0.1.71 | FoxiqMP / Windows QT / #LiberVixer | Оранжево-зелёная |
| FoxiqMP qt, Linux | 0.1.71 | FoxiqMP / Linux QT / #LiberVixer | Оранжево-зелёная |

[Графика и запросы imagegen](../branding/README.md),
[сравнение](../branding/preview.png),
[светлые варианты](../branding/preview-light.png).

Реальные окна:
[Classic](images/foxicmp-interface.png),
[Windows Qt](images/foxiqmp-windows-qt.png),
[Linux Qt](images/foxiqmp-linux-qt.png).
Windows снимались через PrintWindow с отдельных тестовых профилей. Linux —
через QWidget::grab в изолированном Xvfb; системное оформление заголовка X11
в этот снимок не входит. История и личные файлы в снимках отсутствуют.

Проверки:

- Windows Classic Release x64 собран MSVC; новое название присутствует в
  заголовке и ProductName. Байты встроенного FILE/210 совпадают с окончательным
  `src/apps/mplayerc/res/logo.1.png`:
  SHA256 `1a37a1e76ec5ba321ce732d87762be20f1fcfe6e3895229e2b7135edc06e9064`.
- ICO Qt содержит 16, 24, 32, 48, 64, 128, 256. В реальном значке заголовка
  Classic обнаружены 13 насыщенных синих и 0 зелёных пикселей; у Windows Qt —
  14 зелёных и 0 синих.
- Windows Qt: финальный portable-набор **20/20**, код выхода процесса **0**.
- Обе Windows-программы открылись, показали соответствующие платформенные
  логотипы и завершились штатно, exit 0.
- Linux: **7/7** групп — classic-interface, web-control, window-title,
  appearance, input-control, desktop-integration, profile-launch; 332.67 с.
- После окончательной настройки контраста — window-title, **PASS**, 23.52 с,
  реальные снимки тёмной и светлой темы. На чёрном видеополе обе темы сохраняют
  читаемую светлую надпись; в светлом окне настроек превью использует тёмную.
- Каталог English: 1699 исходных строк покрыто, 1846 записей; параметры валидны.
- desktop-файл Linux проходит desktop-file-validate; новый абсолютный путь
  `foxiqmp-qt.png` обновляет значок без зависимости от системной темы.

Установлены Windows-копии `C:\FoxBroxer\Prg\FoxicMP` и
`C:\FoxBroxer\Prg\FoxicMPq`, а также существующая Linux-копия.
Windows INI обеих программ совпали с резервными копиями побайтно. Linux INI
не изменился при атомарной замене исполняемого файла и изображений; сохранения
уже работающего плеера продолжаются обычным образом.

Отображаемые имена обновлены в меню запуска, Qt MPRIS Identity, веб-интерфейсе,
окне «О программе», описании Windows-файла и существующих ключах регистрации Qt.
Пути, имена INI/EXE, GUID установщика Classic, ProgID и IPC сохранены. Harvester
и Classic-профиль в папке MPMY не изменялись. Временная задача Windows и тестовые
EXE/Qt6Test.dll из установленной папки удалены.
