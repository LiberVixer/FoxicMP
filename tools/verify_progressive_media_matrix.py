#!/usr/bin/env python3
"""Check the finite progressive acceptance matrix against source capabilities.

This checks planning/traceability only. It never grants playback acceptance,
starts a player, changes a profile or accesses a Harvester source.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
MATRIX = ROOT / 'docs/ProgressiveRequiredMediaMatrix.json'
EDITIONS = {'linux-qt', 'windows-qt', 'windows-classic'}


def digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(',', ':')).encode()).hexdigest()


def source_contracts():
    spec = importlib.util.spec_from_file_location('progressive_package', ROOT/'tools/package_progressive_sdk.py')
    package = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(package)
    source = (ROOT/'shared/progressivestreamcapabilities.h').read_text()
    return dict(commonNative=package.schema2_playback(), qtNative=package.qt_schema2_playback(),
                classicNative=package.classic_schema2_playback(),
                continuous=json.loads(''.join(re.findall(r'R"\((.*?)\)"', source, re.S))))


def require(ok, message):
    if not ok:
        raise ValueError(message)


def verify(matrix, contracts=None):
    contracts = source_contracts() if contracts is None else contracts
    require(matrix['schemaVersion'] == 1 and matrix['workId'] == 'P02.C149.01', 'Unknown matrix identity')
    require(matrix['productChanged'] is False, 'A planning matrix cannot claim a product implementation')
    registry = json.loads((ROOT/'docs/FoxicMPWorkRegistry.json').read_text())
    nodes = {n['id']: n for n in registry['nodes']}
    groups = ('existingProfiles', 'remainingFamilies', 'negativeCases', 'scenarioAxes', 'exclusions')
    all_rows = [row for key in groups for row in matrix[key]]
    ids = [r['id'] for r in all_rows]
    require(len(ids) == len(set(ids)), 'Duplicate matrix ID')
    expected_sets = {k: sorted(v['profiles']) for k, v in contracts.items()}
    require(matrix['capabilityProfileSets'] == expected_sets, 'Capability profile inventory drift')
    require(matrix['contractDigests'] == {k: digest(v) for k, v in contracts.items()}, 'Capability admission drift')
    expected_native = set(contracts['qtNative']['profiles']) | set(contracts['classicNative']['profiles'])
    expected_continuous = contracts['continuous']['profiles']
    native, continuous = set(), set()
    for row in matrix['existingProfiles']:
        require(row['status'] == 'bounded-existing', 'Existing profile must retain its bounded scope')
        require(row.get('evidence'), 'Existing profile without evidence route')
        for reference in row['evidence']:
            path = Path(reference)
            require(not path.is_absolute() and '..' not in path.parts and (ROOT/path).is_file(), 'Missing evidence file')
        id = row['id']
        if row['path'] == 'native-progressive':
            native.add(id)
            expected = {e for e, k in [('linux-qt', 'qtNative'), ('windows-qt', 'qtNative'), ('windows-classic', 'classicNative')]
                        if id in contracts[k]['profiles']}
        else:
            require(row['path'] == 'progressive-remux' and id in expected_continuous, 'Unknown remux profile')
            continuous.add(id)
            expected = EDITIONS
            require(row['admission'] == expected_continuous[id], 'Altered remux admission')
        require(set(row['declaredEditions']) == expected, 'Unproven edition declaration')
    require(native == expected_native and continuous == set(expected_continuous), 'Missing/extra bounded profiles')
    common_gates = set(matrix['commonGates'])
    require(common_gates == {'confirmed-io','packet-copy','decode-reference','incomplete-playback',
                             'timeline-controls','lifecycle','negative-cases','resource-bounds'}, 'Undefined acceptance gate set')
    required_families = {'M01','M02','M03','M04','W01','W02','W03','K01','K02','K03',
                         'T01','T02','T03','A01','A02','A03','A04','A05','A06','A07','A08',
                         'I01','S01','I02','V01','Q01','U01','U02','D01','L01'}
    require({r['id'] for r in matrix['remainingFamilies']} == required_families, 'Frozen input-family scope changed')
    require({r['id'] for r in matrix['scenarioAxes']} == {f'X{i:02}' for i in range(1,9)}, 'Frozen cross-cutting scope changed')
    for row in matrix['remainingFamilies']:
        require(row['status'] in ('not-accepted', 'accepted-bounded'), 'A planned family is not a playback PASS')
        require(row['requirement'] in ('mandatory', 'conditional-local-or-service-corpus'), 'Invalid requirement class')
        require(set(row['editions']) == EDITIONS, 'Required family missing an edition')
        require(row['inputs'] and row['layouts'] and len(row['layouts']) == len(set(row['layouts'])), 'Undefined/duplicate layout cases')
        require(set(row['gates']) == common_gates, 'Missing mandatory acceptance gate')
        if row['status'] == 'accepted-bounded':
            reference = row.get('acceptanceEvidence', '')
            path = Path(reference)
            require(reference and not path.is_absolute() and '..' not in path.parts and (ROOT/path).is_file(), 'Missing family acceptance evidence')
            evidence = json.loads((ROOT/path).read_text())
            require(evidence.get('result') == 'PASS' and row['id'] in evidence.get('acceptedFamilies', []), 'Family evidence is not a PASS')
            require(set(evidence.get('editions', {})) == EDITIONS, 'Family evidence missing an edition')
            for edition in EDITIONS:
                accepted = evidence['editions'][edition]
                require(accepted.get('result') == 'PASS' and accepted.get('exitCode') == 0 and
                        accepted.get('gates') == {g: True for g in common_gates}, 'Family evidence missing acceptance gates')
            require(row.get('boundedScope'), 'Accepted family must retain its bounded scope')
        for pair in row['inputs'].get('pairs', []):
            require(isinstance(pair, list) and len(pair) == 2 and all(isinstance(s, str) and s for s in pair), 'Invalid explicit codec pair')
    for row in matrix['remainingFamilies'] + matrix['scenarioAxes']:
        require(row['owners'] and len(row['owners']) == len(set(row['owners'])), 'Missing/duplicate work owners')
        for owner in row['owners']:
            require(owner in nodes and 'status' in nodes[owner], 'Owner must be a terminal work ID')
        if 'cases' in row:
            require(row['cases'] and len(row['cases']) == len(set(row['cases'])), 'Undefined/duplicate scenario cases')
    section = (ROOT/'docs/ProgressiveTechnicalSpec-2026-10-25.ru.md').read_text().split('## 5. ')[1].split('## 6. ')[0]
    areas = set(re.findall(r'^\| ([^|]+) \|', section, re.M)) - {'Область'}
    areas = {x.strip() for x in areas if not x.strip().startswith('-')}
    require(set(matrix['specCoverage']) == areas, 'Missing media-spec area')
    for targets in matrix['specCoverage'].values():
        require(targets and all(t in ids for t in targets), 'Invalid specification route')
    require({r['expectedClass'] for r in matrix['negativeCases']} == {
        'encrypted-source', 'corrupt-source', 'missing-decoder', 'waiting-initialization',
        'waiting-index', 'waiting-keyframe', 'waiting-range', 'source-identity-error',
        'resource-limit', 'unsupported-layout', 'writer-error-not-eof', 'post-download'}, 'Missing negative outcome class')
    require('7.1' not in matrix['variantRules']['channelValidity']['ac3'], 'Invalid AC-3 positive channel layout')
    require(matrix['variantRules']['mp3Rates'] == dict(mpeg1=[32000,44100,48000], mpeg2=[16000,22050,24000], **{'mpeg2.5':[8000,11025,12000]}), 'MPEG rates must not be a Cartesian product')
    for exclusion in matrix['exclusions']:
        require(exclusion['classification'] in ('objective-negative', 'deferred-by-user', 'excluded-by-user',
                                                 'external-blocked', 'conditional-admission',
                                                 'outside-positive-core-not-universal-unsupported'), 'Ambiguous exclusion')
        require(exclusion['subjects'] and exclusion['reason'], 'Unexplained exclusion')
    require(matrix['executionOrder'][0] == matrix['workId'], 'Matrix dependency must come first')
    require(all(id in nodes for id in matrix['executionOrder']), 'Unknown execution ID')
    return dict(result='PASS', validationKind='planning-and-source-contract-only',
                boundedExisting=len(matrix['existingProfiles']), native=len(native), continuous=len(continuous),
                declaredFamilies=len(matrix['remainingFamilies']),
                remainingFamilies=sum(r['status'] == 'not-accepted' for r in matrix['remainingFamilies']),
                acceptedFamilies=sum(r['status'] == 'accepted-bounded' for r in matrix['remainingFamilies']), scenarioAxes=len(matrix['scenarioAxes']),
                negativeClasses=len(matrix['negativeCases']), specificationAreas=len(areas),
                newPlaybackAccepted=False, playersStarted=False, harvesterChanged=False)


def render(matrix):
    summary = verify(matrix)
    lines = ['# Зафиксированная медиа-матрица progressive-приёмки', '',
             '17 ноября 2026. Задача **P02.C149.01**, база SDK1.106.',
             'Машиночитаемый источник: [ProgressiveRequiredMediaMatrix.json](ProgressiveRequiredMediaMatrix.json).', '',
             f"Зафиксировано {summary['boundedExisting']} существующих ограниченных профилей: {summary['native']} native и {summary['continuous']} continuous-remux.",
             f"Остаются {summary['remainingFamilies']} из {summary['declaredFamilies']} именованных входных семейств; к ним применяются {summary['scenarioAxes']} сквозных групп и {summary['negativeClasses']} классов отрицательных исходов.",
             'Это строки корпуса приёмки, **не новые задачи реестра**. Каждая привязана к существующим ID.',
             'Сама проверка матрицы не запускает плееры. Подтверждённые семейства имеют отдельные runtime отчёты; общие P02.C148.01–03 остаются открытыми.', '',
             '## Порядок и критерий закрытия', '',
             matrix.get('executionNote', 'Первый конкретный вход — P02.C149.02: обычный M4A/audio-only, строки M01/M02.'),
             'Новый codec/profile не закрывает семейство целиком без отдельной приёмки.',
             'Готовность реализации требует всех применимых положительных и отрицательных',
             'строк её владельца на Linux Qt, Windows Qt и Classic отдельно.', '',
             'Неподдержанный сейчас обязательный профиль остаётся OPEN. Наличие похожего native',
             'профиля, demuxer FFmpeg, probe или воспроизведение готового файла не закрывают строку.',
             'Потерянный обязательный fixture — блокер, а не успешный SKIP. Приёмка final release',
             'и физический A/V выход считаются отдельно.', '',
             '## Общие проверки каждой положительной строки', '']
    lines += ['- **'+key+'**: '+text+'.' for key, text in matrix['commonGates'].items()]
    lines += ['', '## Оставшиеся входные семейства', '',
              '| Строка | Вход | Пункты реестра | Условие / статус |', '|---|---|---|---|']
    for row in matrix['remainingFamilies']:
        condition = 'обязательно' if row['requirement'] == 'mandatory' else 'предусмотренный локальный/сервисный корпус'
        status = 'принято в указанном объёме' if row['status'] == 'accepted-bounded' else 'открыто'
        lines.append('| '+row['id']+' | '+row['title']+' | '+', '.join(row['owners'])+' | '+condition+'; '+status+' |')
    lines += ['', '### Точные входы и варианты', '',
              'В JSON массивы не образуют произвольное декартово произведение. Например, AC-3',
              'не получает положительный7.1 случай из общего списка AC-3/E-AC-3; MP3 версии',
              'имеют отдельные наборы частот. Допустимость пары и CodecPrivate проверяются по bytes.',
              'Не перечисленная здесь комбинация не объявляется навсегда неподдерживаемой.', '']
    for row in matrix['remainingFamilies']:
        lines.extend(['**'+row['id']+'. '+row['title']+'**', '',
                      'Входы: `'+json.dumps(row['inputs'], ensure_ascii=False, separators=(',', ':'))+'`.',
                      'Варианты: '+', '.join(row['layouts'])+'.', ''])
        if row['status'] == 'accepted-bounded':
            lines.extend(['Подтверждённый объём: '+row['boundedScope']+'.',
                          'Отчёт: ['+Path(row['acceptanceEvidence']).name+']('+Path(row['acceptanceEvidence']).name+').', ''])
    lines += ['## Сквозные сценарии', '']
    for axis in matrix['scenarioAxes']:
        lines += ['- **'+axis['id']+' — '+axis['title']+'**: '+', '.join(axis['cases'])+'.',
                  '  Реестр: '+', '.join(axis['owners'])+'.']
    lines += ['', '## Отрицательные исходы', '']
    lines += ['- **'+r['id']+' / '+r['expectedClass']+'**: '+r['title']+'.' for r in matrix['negativeCases']]
    lines += ['', 'Это классы результата приёмки. Конкретный wire reasonCode берётся из',
              'контракта режима; нельзя переименовывать действующий API ради этой таблицы.', '',
              '## Отложенное и объективные границы', '']
    for e in matrix['exclusions']:
        lines += ['- **'+e['id']+' / '+e['classification']+'**: '+', '.join(e['subjects'])+'. '+e['reason']+'.']
    lines += ['', '## Уже заявленные ограниченные профили', '',
              'Список ниже сверяется с исходными capabilities, а не с расширениями файлов.',
              'Он сохраняет ограничения отчётов на их дату. Новая общая приёмка не заявляется.',
              'Полный admission continuous-профилей и fingerprints native contracts сохранены в JSON.', '',
              '| Profile ID | Путь | Декларация редакций | Контракт/отчёт |', '|---|---|---|---|']
    for row in matrix['existingProfiles']:
        links = ', '.join('['+Path(p).name+']('+Path(p).name+')' for p in row['evidence'])
        lines.append('| `'+row['id']+'` | '+row['path']+' | '+', '.join(row['declaredEditions'])+' | '+links+' |')
    lines += ['', '## Изменение матрицы', '',
              'Изменение capability или списка профилей требует явного обновления этой матрицы.',
              'Обязательные строки не удаляются ради зелёного отчёта. Новый формат из реального',
              'YT/VK/Rutube корпуса добавляется revision с причиной и соответствующим ID реестра;',
              'его отсутствие в этой базе не отменяет требование ТЗ обработать новый вход.',
              'Медиа-строка и отдельный повтор теста не создают автоматически новую задачу.',
              'Проверка и генерация: `python3 tools/verify_progressive_media_matrix.py --write`.', '',
              'Основания: [ТЗ, разделы5–14](ProgressiveTechnicalSpec-2026-10-25.ru.md),',
              '[Matroska Codec Mapping](https://www.matroska.org/technical/codec_specs.html),',
              '[FFmpeg libavformat API](https://ffmpeg.org/doxygen/trunk/avformat_8h.html).',
              'Спецификация контейнера и наличие demuxer сами по себе не доказывают playback FoxicMP.', '']
    return '\n'.join(lines)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--matrix', type=Path, default=MATRIX)
    p.add_argument('--write', action='store_true')
    args = p.parse_args()
    data = json.loads(args.matrix.read_text())
    result = verify(data)
    if args.write:
        (ROOT/'docs/ProgressiveRequiredMediaMatrix.md').write_text(render(data), encoding='utf-8')
    print(json.dumps(result, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
