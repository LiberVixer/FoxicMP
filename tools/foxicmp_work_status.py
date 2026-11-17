#!/usr/bin/env python3
"""Validate and report FoxicMP's documented work hierarchy; no player mutations."""
import argparse
from collections import Counter, defaultdict
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
REGISTRY = ROOT / 'docs/FoxicMPWorkRegistry.json'
BASELINE = ROOT / 'docs/FoxicMPWorkBaseline-2026-11-17.json'
LAST_REPORT = ROOT / 'docs/FoxicMPWorkLastReport.json'
REPORT = ROOT / 'docs/FoxicMPWorkChecklist.md'
REMAINING = ROOT / 'docs/FoxicMPWorkRemaining.md'
STATES = ('done', 'todo', 'partial', 'blocked', 'deferred')
LABELS = dict(done='готово', todo='осталось', partial='частично',
              blocked='внешнее условие', deferred='отложено')


def read(path):
    return json.loads(Path(path).read_text(encoding='utf-8'))


def inventory(data, root=ROOT):
    nodes = data['nodes']
    by_id = {n['id']: n for n in nodes}
    if len(nodes) != len(by_id):
        raise ValueError('Duplicate work IDs')
    children = defaultdict(list)
    for n in nodes:
        if not n['title'].strip() or not n.get('sources'):
            raise ValueError('Missing title/evidence route: ' + n['id'])
        parent = n['parent']
        if parent is not None and parent not in by_id:
            raise ValueError('Missing parent: ' + n['id'])
        children[parent].append(n['id'])
        for source in n['sources']:
            path = Path(source['path'])
            if path.is_absolute() or '..' in path.parts or not (root / path).is_file():
                raise ValueError('Invalid evidence route: ' + str(path))
    depth, status = {}, {}

    def visit(id, level, ancestors):
        if id in ancestors or id in depth or level > 3:
            raise ValueError('Cycle, duplicate traversal or depth >3: ' + id)
        depth[id] = level
        descendants = children[id]
        node = by_id[id]
        if descendants:
            if 'status' in node:
                raise ValueError('Parent status must be computed: ' + id)
            values = [visit(c, level + 1, ancestors | {id}) for c in descendants]
            if all(v == 'done' for v in values):
                value = 'done'
            elif all(v in ('done', 'deferred') for v in values):
                value = 'deferred'
            elif all(v in ('done', 'blocked', 'deferred') for v in values):
                value = 'blocked'
            elif 'done' in values or 'partial' in values:
                value = 'partial'
            else:
                value = 'todo'
        else:
            value = node.get('status')
            if value not in STATES or value == 'partial':
                raise ValueError('Leaf needs an atomic status: ' + id)
            if value in ('blocked', 'deferred') and not node.get('reason'):
                raise ValueError('Missing reason: ' + id)
        status[id] = value
        return value

    for id in children[None]:
        visit(id, 1, set())
    if len(depth) != len(nodes):
        raise ValueError('Unreachable nodes/cycle')
    # Every checkbox in the registered source lists must be migrated exactly
    # once. An alias points at existing work, never adds a new counted entity.
    coverage = defaultdict(dict)
    for item in data['sourceMappings']:
        key = item['textSha256']
        if key in coverage[item['path']]:
            raise ValueError('Duplicate source migration')
        coverage[item['path']][key] = item
        if not item['targets'] or any(t not in by_id for t in item['targets']):
            raise ValueError('Unresolved source mapping')
    for path, mappings in coverage.items():
        source = (root / path).read_text().splitlines()
        actual = [hashlib.sha256(s.encode()).hexdigest() for s in source
                  if re.match(r'\s*- \[[ x]\]', s)]
        if len(actual) != len(set(actual)) or set(actual) != set(mappings):
            raise ValueError('Unmapped/changed source checkboxes: ' + path)
    for path in (root / 'docs').glob('*Checklist.md'):
        if path.name == REPORT.name:
            continue
        if re.search(r'^\s*- \[[ x]\]', path.read_text(), re.M):
            if str(path.relative_to(root)) not in coverage:
                raise ValueError('Unregistered checkbox list: ' + path.name)
    sections = set(re.findall(r'^## (\d+)\.',
                             (root / 'docs/ProgressiveTechnicalSpec-2026-10-25.ru.md').read_text(), re.M))
    if set(data['specSections']) != sections:
        raise ValueError('Unmapped specification section')
    for targets in data['specSections'].values():
        if any(t not in by_id for t in targets):
            raise ValueError('Invalid spec route')
    return by_id, children, depth, status


def counts(ids, status):
    c = Counter(status[id] for id in ids)
    total = sum(c.values())
    return dict(total=total, done=c['done'], remaining=total-c['done'],
                active=c['todo']+c['partial'], blocked=c['blocked'], deferred=c['deferred'])


def snapshot(data):
    by_id, children, depth, status = inventory(data)
    return dict(schemaVersion=1, baselineDate=data['baselineDate'],
                levels={str(n): counts([id for id in by_id if depth[id] == n], status) for n in (1, 2, 3)},
                nodes={id: dict(level=depth[id], status=status[id], title=by_id[id]['title'],
                                terminal=not children[id],
                                additionType=by_id[id].get('additionType', 'migration'),
                                changeReason=by_id[id].get('changeReason', '')) for id in by_id},
                total=counts(by_id, status),
                leaves=counts([id for id in by_id if not children[id]], status),
                sourceCheckboxes=len(data['sourceMappings']),
                sourceLists=len({s['path'] for s in data['sourceMappings']}),
                aliases=sum(x['kind'] == 'alias' for x in data['sourceMappings']))


def changes(current, previous):
    now, old = current['nodes'], previous['nodes']
    added, removed = sorted(now.keys()-old.keys()), sorted(old.keys()-now.keys())
    closed = sorted(id for id in now.keys() & old.keys() if now[id]['status'] == 'done' and old[id]['status'] != 'done')
    reopened = sorted(id for id in now.keys() & old.keys() if now[id]['status'] != 'done' and old[id]['status'] == 'done')
    moved = sorted(id for id in now.keys() & old.keys() if now[id]['level'] != old[id]['level'])
    retitled = sorted(id for id in now.keys() & old.keys() if now[id]['title'] != old[id]['title'])
    for id in added:
        if now[id]['additionType'] not in ('new-requirement', 'decomposition', 'omission') or not now[id]['changeReason'].strip():
            raise ValueError('New ID needs additionType and changeReason: ' + id)
    for id in reopened:
        if now[id]['terminal'] and not now[id]['changeReason'].strip():
            raise ValueError('Reopened work needs changeReason: ' + id)
    for id in retitled:
        if not now[id]['changeReason'].strip():
            raise ValueError('Changed work definition needs changeReason: ' + id)
    return dict(added=added, removed=removed, closed=closed, reopened=reopened, moved=moved, retitled=retitled,
                addedByLevel={str(n): sum(now[id]['level'] == n for id in added) for n in (1, 2, 3)},
                addedByType=dict(Counter(now[id]['additionType'] for id in added)),
                closedByLevel={str(n): sum(now[id]['level'] == n for id in closed) for n in (1, 2, 3)})


def render(data, current, delta):
    by_id, children, depth, status = inventory(data)
    lines = ['# FoxicMP: единый учёт работ', '',
             'База пересчёта — 17 ноября 2026, SDK1.106. Реестр охватывает известные работы',
             'progressive-ТЗ и семейного плана. Он считает результаты работ, а не число тестов.',
             'Статусы восстановлены по имеющимся отчётам; новый полный runtime-аудит не проводился.', '',
             '## Текущий счётчик', '',
             '| Уровень | Всего | Готово | Не завершено | Активный план | Внешнее условие | Отложено |',
             '|---|---:|---:|---:|---:|---:|---:|']
    for level, name in [('1', 'Задачи'), ('2', 'Подзадачи'), ('3', 'Подподзадачи')]:
        c = current['levels'][level]
        lines.append(f"| {name} | {c['total']} | {c['done']} | {c['remaining']} | {c['active']} | {c['blocked']} | {c['deferred']} |")
    for name, key in [('Все узлы трёх уровней', 'total'), ('Только конечные рабочие пункты', 'leaves')]:
        c = current[key]
        lines.append(f"| **{name}** | **{c['total']}** | **{c['done']}** | **{c['remaining']}** | {c['active']} | {c['blocked']} | {c['deferred']} |")
    lines += ['', 'Родитель и его дети — разные уровни, поэтому их сумма не является оценкой',
              'трудозатрат или процентом готовности продукта. Для движения работы полезнее',
              'конечные пункты. Родитель закрывается только после всех своих детей.', '',
              '## Изменения относительно базы', '',
              f"Добавлено задач / подзадач / подподзадач: **{delta['addedByLevel']['1']} / {delta['addedByLevel']['2']} / {delta['addedByLevel']['3']}**.",
              f"Закрыто с базы по уровням: **{delta['closedByLevel']['1']} / {delta['closedByLevel']['2']} / {delta['closedByLevel']['3']}**.",
              f"Удалено: {len(delta['removed'])}; повторно открыто: {len(delta['reopened'])}; сменило уровень: {len(delta['moved'])}; изменено описание: {len(delta['retitled'])}.", '',
              'Сам исходный пересчёт — миграция известных работ, а не появление новых требований.',
              'Отдельно: [только оставшиеся конечные пункты](FoxicMPWorkRemaining.md),',
              '[проверка пересчёта](FoxicMPWorkRecount-2026-11-17.json).',
              'При последующем добавлении указываются ID и причина: новое требование,',
              'детализация существующего пункта либо исправление пропуска учёта.', '',
              '## Почему прежние77 не показывали движение', '',
              f"Все {current['sourceCheckboxes']} checkbox-записи {current['sourceLists']} старых списков связаны с этим реестром;",
              f"{current['aliases']} записей — повтор/общий критерий или ограничение, а не новая единица работы.",
              'В старом общем журнале 149 готовых записей и 77 открытых оставались неизменными.',
              '98 поздних выполненных шагов VFR/SimpleBlock/video count/nominal/audio count/Opus',
              'жили в отдельных чеклистах. Здесь они учтены внутри своих законченных этапов.',
              'Сами 77 — историческое состояние, больше не основной рабочий счётчик.', '',
              '## Все задачи', '',
              '| ID | Задача | Статус | Конечных пунктов готово / всего | Активно осталось | Внешнее условие | Отложено |',
              '|---|---|---|---:|---:|---:|---:|']

    def leaves(id):
        return [x for c in children[id] for x in leaves(c)] if children[id] else [id]

    for id in children[None]:
        c = counts(leaves(id), status)
        lines.append(f"| {id} | {by_id[id]['title']} | {LABELS[status[id]]} | {c['done']} / {c['total']} | {c['active']} | {c['blocked']} | {c['deferred']} |")
    lines += ['', '## Полный список с подзадачами', '',
              'Готовые результаты сохраняют точные границы исходных отчётов. Они не обещают',
              'непроверенные сочетания кодеков, аппаратный вывод или настоящий Harvester.', '']

    def emit(id):
        n = by_id[id]
        sources = ', '.join(f"[{Path(s['path']).name}{':' + str(s['line']) if s.get('line') else ''}]({Path(s['path']).name})" for s in n['sources'])
        title = n['title'].replace('\n', ' ').rstrip(' .')
        lines.append('  ' * (depth[id]-1) + f"- **{id}** — **{LABELS[status[id]]}**: {title}. Источник: {sources}.")
        if n.get('reason'):
            lines.append('  ' * depth[id] + n['reason'])
        for child in children[id]:
            emit(child)

    for id in children[None]:
        emit(id)
        lines.append('')
    lines += ['## Границы и порядок обновления', '']
    lines += ['- ' + x for x in data['scopeNotes']]
    lines += ['- ' + x['title'] for x in data['constraints']]
    lines += ['', 'Числа вычисляются из [JSON-реестра](FoxicMPWorkRegistry.json), не правятся вручную.',
              'Проверка/сводка: `python3 tools/foxicmp_work_status.py`.',
              'Обновление этого документа: `python3 tools/foxicmp_work_status.py --write`.',
              'Сравнение с последним сохранённым отчётом: `--compare ПУТЬ_К_СНИМКУ`.',
              'Новый снимок: `--snapshot ПУТЬ_К_НОВОМУ_СНИМКУ` (существующий не перезаписывается).',
              'Перед сохранением снимка показать пользователю added/closed/reopened и причины новых пунктов.',
              'Сохранить итог задания и обновить точку сравнения: `--write --record-report`.',
              'По умолчанию сравнение идёт с последним отчётом; исходная база остаётся неизменной.',
              'Новый ID требует `additionType` = `new-requirement`, `decomposition` или `omission`',
              'и непустую `changeReason`. Удаление и повторное открытие также требуют причины.',
              'Статусы листьев меняются только со ссылкой на проверку результата. Повтор теста',
              'добавляет доказательство к прежнему ID, а не новую выполненную задачу.',
              'После задания выводить три уровня всего/готово/осталось и добавленные пункты;',
              'промежуточные сообщения счётчиком не перегружать.', '']
    return '\n'.join(lines)


def render_remaining(data, current):
    by_id, children, depth, status = inventory(data)
    c = current['leaves']
    lines = ['# FoxicMP: что ещё нужно сделать', '',
             f"Из {c['total']} конечных пунктов готово {c['done']}, не завершено {c['remaining']}.",
             f"Активный план: {c['active']}; внешние условия: {c['blocked']}; отложено: {c['deferred']}.",
             'Общие родители здесь служат навигацией; они не добавляются к числу конечных пунктов.',
             '[Полный счётчик и выполненные работы](FoxicMPWorkChecklist.md).', '',
             '«Внешнее условие» и «отложено» не разрешают автоматически менять Harvester,',
             'проводить аппаратные испытания или возвращаться к исключённому Wayland.', '']

    def emit(id):
        if status[id] == 'done':
            return
        node = by_id[id]
        title = node['title'].rstrip(' .')
        if depth[id] == 1:
            lines.extend([f"## {id}. {title}", ''])
        elif children[id]:
            lines.extend([f"**{id}. {title}**", ''])
        else:
            source = node['sources'][0]['path']
            lines.append(f"- **{id}** — {LABELS[status[id]]}: {title}. [Основание]({Path(source).name}).")
            if node.get('reason'):
                lines.append('  ' + node['reason'])
        for child in children[id]:
            emit(child)
        if children[id]:
            lines.append('')

    for id in children[None]:
        emit(id)
    return '\n'.join(lines)+'\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--registry', type=Path, default=REGISTRY)
    parser.add_argument('--compare', type=Path, default=LAST_REPORT)
    parser.add_argument('--snapshot', type=Path)
    parser.add_argument('--write', action='store_true')
    parser.add_argument('--record-report', action='store_true')
    args = parser.parse_args()
    data = read(args.registry)
    current = snapshot(data)
    if args.snapshot:
        with args.snapshot.open('x', encoding='utf-8') as stream:
            stream.write(json.dumps(current, ensure_ascii=False, indent=2)+'\n')
    if not args.compare.is_file():
        raise ValueError('Comparison snapshot does not exist; create baseline with --snapshot')
    previous = read(args.compare)
    delta = changes(current, previous)
    for id in delta['removed']:
        if not data.get('retired', {}).get(id, '').strip():
            raise ValueError('Removed work needs a retired reason: ' + id)
    if args.write:
        REPORT.write_text(render(data, current, delta), encoding='utf-8')
        REMAINING.write_text(render_remaining(data, current), encoding='utf-8')
    if args.record_report:
        current['reportedAtUtc'] = datetime.now(timezone.utc).isoformat()
        archive = ROOT / 'docs/work-reports'
        archive.mkdir(exist_ok=True)
        name = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')+'.json'
        changed = set(delta['added']+delta['closed']+delta['reopened']+delta['moved']+delta['retitled'])
        record = dict(reportedAtUtc=current['reportedAtUtc'], levels=current['levels'],
                      total=current['total'], leaves=current['leaves'], changes=delta,
                      definitions={id: current['nodes'][id] for id in sorted(changed)},
                      registrySha256=hashlib.sha256(args.registry.read_bytes()).hexdigest())
        with (archive/name).open('x', encoding='utf-8') as stream:
            stream.write(json.dumps(record, ensure_ascii=False, indent=2)+'\n')
        LAST_REPORT.write_text(json.dumps(current, ensure_ascii=False, indent=2)+'\n')
    print(json.dumps(dict(levels=current['levels'], total=current['total'], leaves=current['leaves'],
                          changes=delta, sourceCheckboxes=current['sourceCheckboxes'], aliases=current['aliases']),
                     ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
