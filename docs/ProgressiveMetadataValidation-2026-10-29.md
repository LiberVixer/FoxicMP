# Metadata и главы — валидация 2026-10-29

- Общий native media corpus: 463/463 PASS; проверены metadata revision, порядок глав и позднее добавление главы.
- Linux Qt: `progressive-confirmed-seek` 1/1 PASS; status содержит metadata, timeline и меню глав обновляются.
- Windows Qt: `progressive-confirmed-seek` 1/1 PASS (13 data cases); status/timeline path compiled and tested.
- Windows Classic: отдельная Classic-сборка после изменения прошла; обновление выполняется через штатный `IDSMChapterBag`.
- YouTubeHarvester не изменялся; установленные Windows-профили не изменялись.

Невалидная metadata отклоняется общим валидатором. HLS/DASH chapters, live window,
обложки и embedded container metadata остаются отдельными этапами.
