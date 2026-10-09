"""Build a navigable document catalog and static LaTeX source scope (no compilation)."""
import csv
import hashlib
import json
import os
from pathlib import Path
import re

BASE = Path(__file__).resolve().parents[1]
EVIDENCE = BASE / 'evidence'

def link(p):
    return '<' + os.path.relpath(p, BASE).replace('\\', '/') + '>'

def main():
    with (EVIDENCE / 'local-files.csv').open(encoding='utf-8-sig', newline='') as f:
        files = list(csv.DictReader(f))
    selected = []
    for r in files:
        p = Path(r['root']) / r['path']
        if p.suffix.lower() not in {'.md', '.tex', '.bib', '.rst'}: continue
        if r['root'].lower().endswith('stardis-gpu') and r['path'].split('/')[0].startswith('stardis-'): continue
        if any((x.lower().startswith('build') and x != 'build_migration') or x == 'Figures - bak' for x in p.parts): continue
        if '/.github/' in p.as_posix() or '/.opencode/' in p.as_posix(): continue
        try:
            content = p.read_text(encoding='utf-8-sig', errors='replace')
            title = next((l.strip().lstrip('#').strip() for l in content.splitlines() if l.startswith('# ')), p.name)
        except OSError:
            title = p.name
        selected.append((r['root'], r['path'], title, p))
    text = ['# 现有文档逐文件目录', '', '按本地实际文件生成。正文及图表的版本以原文件为准；目录名称中的 TODO、RESOLVED、历史方案标签原样保留。此索引不将历史结论提升为当前版本结论。', '', '返回 [总目录](README.md)。', '']
    for root in sorted({x[0] for x in selected}):
        text += ['## ' + Path(root).name, '', '| 文件 | 文档标题 |', '|---|---|']
        for _, rel, title, p in [x for x in selected if x[0] == root]:
            text.append(f'| [{rel}]({link(p)}) | {title.replace("|", "/")} |')
        text.append('')
    (BASE / '07-document-catalog.md').write_text('\n'.join(text) + '\n', encoding='utf-8')

    thesis = Path('D:/thesis')
    refs = []
    # Scan literal references in actual chapters + main + template; macro expansion is out of scope.
    tex_sources = [thesis / 'MainBody.tex', *sorted((thesis / 'Chapters').glob('*.tex')),
                   *sorted((thesis / 'Template').glob('*.cls')), *sorted((thesis / 'Template').glob('*.tex'))]
    for p in tex_sources:
        content = p.read_text(encoding='utf-8-sig', errors='replace')
        content = re.sub(r'(?<!\\)%[^\n]*', '', content)
        for m in re.finditer(r'\\(includegraphics\*?|include|input|bibliography)(?:\s*\[[^\]]*\])?\s*\{([^{}]+)\}', content):
            cmd, target = m.groups()
            if '\\' in target or '#' in target:
                refs.append({'source': p.relative_to(thesis).as_posix(), 'command': cmd, 'target': target, 'status': 'dynamic-review', 'resolved': []})
                continue
            paths = [thesis / target, p.parent / target]
            if not Path(target).suffix:
                extensions = ['.png', '.pdf', '.jpg', '.jpeg', '.eps'] if cmd.startswith('includegraphics') else ['.bib'] if cmd == 'bibliography' else ['.tex']
                paths = [Path(str(path) + ext) for path in paths for ext in extensions]
            found = sorted({str(path.resolve()) for path in paths if path.is_file()})
            refs.append({'source': p.relative_to(thesis).as_posix(), 'command': cmd, 'target': target, 'status': 'found' if found else 'missing', 'resolved': found})
    (EVIDENCE / 'thesis-references.json').write_text(json.dumps({'scope': 'Static literal references only; includes unreferenced chapter/template definitions; not a TeX compilation', 'references': refs}, ensure_ascii=False, indent=2), encoding='utf-8')
    source_rows = []
    exclude_ext = {'.aux','.bbl','.blg','.fdb_latexmk','.fls','.lof','.log','.lot','.toc','.xdv','.dvi','.bak','.out','.synctex','.gz'}
    auxiliary_dirs = {'OurWork','References','ReviewComments','report','ref_need'}
    required_graphics = {str(Path(p).resolve()) for r in refs if r['command'].startswith('includegraphics') for p in r['resolved']}
    for row in [r for r in files if Path(r['root']) == thesis]:
        rel = Path(row['path'])
        p = thesis / rel
        if rel.parts[0] == 'Figures - bak' or rel.suffix.lower() in exclude_ext or rel.parts[0] == '.vscode':
            dest, reason = 'exclude-generated-backup', '编译辅助文件、备份或本机配置'
        elif len(rel.parts) == 1 and rel.suffix.lower() == '.pdf':
            dest, reason = 'exclude-thesis-output', '整篇论文生成结果，按用户要求不发布'
        elif rel.parts[0] in auxiliary_dirs:
            dest, reason = 'research-hub', '研究笔记、评审、答辩、文献整理，保留并转入成果总库'
        elif rel.parts[0] == '.github':
            dest, reason = 'review-template-workflow', '继承模板工作流，逐项核实与当前论文是否相关'
        elif rel.parts[0] == 'Reference Document':
            dest, reason = 'review-third-party', '学校模板规范资料，核实再分发来源'
        else:
            dest, reason = 'latex-source-candidate', '源工程及支撑资源；最终白名单需编译验证'
        if str(p.resolve()) in required_graphics:
            dest, reason = 'latex-source-required-figure', '正文或模板直接引用的必要插图，不是整篇论文输出'
        source_rows.append({'path': row['path'], 'bytes': row['bytes'], 'destination': dest, 'reason': reason})
    with (EVIDENCE / 'thesis-source-scope.csv').open('w', encoding='utf-8-sig', newline='') as f:
        w = csv.DictWriter(f, fieldnames=['path','bytes','destination','reason']); w.writeheader(); w.writerows(source_rows)
    # Hash a small, useful set of evidence, not multi-GiB profiles.
    hashes=[]
    for r in files:
        p = Path(r['root']) / r['path']
        if ((r['path'].startswith('physical_consistency_stats/') and p.suffix == '.csv')
            or (Path(r['root']) == thesis and (r['path'].startswith('Chapters/') and p.suffix == '.tex' or r['path'] in {'MainBody.tex','ReferenceBase.bib'}))):
            hashes.append({'root': r['root'], 'path': r['path'], 'sha256': hashlib.sha256(p.read_bytes()).hexdigest()})
    (EVIDENCE / 'selected-sha256.json').write_text(json.dumps(hashes, ensure_ascii=False, indent=2), encoding='utf-8')
    print(json.dumps({'documents': len(selected), 'latex_references': len(refs), 'missing_literal_refs': [r for r in refs if r['status']=='missing'], 'scope': {key: {'files': sum(r['destination']==key for r in source_rows), 'mib': round(sum(int(r['bytes']) for r in source_rows if r['destination']==key)/1024**2,2)} for key in sorted({r['destination'] for r in source_rows})}},ensure_ascii=False))

if __name__ == '__main__': main()
