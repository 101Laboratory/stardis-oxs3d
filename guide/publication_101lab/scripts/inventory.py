"""Read-only inventory of research roots; writes reports only under --output.

Python stdlib only. No fetch, checkout, upload, deletion, or git configuration writes.
Public GitHub API cannot establish whether an inaccessible repository is private.
"""
import argparse
import csv
import json
import os
from pathlib import Path
import subprocess
import urllib.request
from datetime import datetime, timezone
from collections import defaultdict

SKIP = {'.git', '.venv', 'venv', 'node_modules', '__pycache__', '.pytest_cache'}
SOURCE = {'.c', '.h', '.cpp', '.hpp', '.cu', '.cuh', '.py', '.ps1', '.bat', '.cmake', '.sh'}
DOC = {'.md', '.tex', '.bib', '.rst', '.docx', '.pptx'}
DATA = {'.csv', '.tsv', '.ht', '.npy', '.npz', '.json', '.sqlite', '.db'}
PROFILE = {'.ncu-rep', '.nsys-rep', '.diagsession', '.etl'}
IMAGES = {'.png', '.jpg', '.jpeg', '.svg', '.pdf', '.ppm', '.exr', '.hdr'}

def git(root, *args):
    try:
        p = subprocess.run(['git', '-c', 'safe.directory=' + str(root).replace('\\', '/'),
                            '-c', 'core.quotepath=false', '-C', str(root), *args],
                           capture_output=True, encoding='utf-8', errors='replace', timeout=35,
                           env={**os.environ, 'GIT_OPTIONAL_LOCKS': '0', 'GIT_TERMINAL_PROMPT': '0'})
        return {'ok': p.returncode == 0, 'text': p.stdout.strip(), 'error': p.stderr.strip()}
    except Exception as e:
        return {'ok': False, 'text': '', 'error': str(e)}

def category(rel):
    p = Path(rel)
    parts = [x.lower() for x in p.parts]
    ext = p.suffix.lower()
    if any(x.startswith('build') or x in {'debug', 'release', 'x64', 'cmakefiles'} for x in parts):
        return 'build-or-build-docs-review'
    if ext in {'.obj', '.pdb', '.ilk', '.exe', '.dll', '.lib', '.xdv', '.aux', '.fls', '.toc', '.log', '.bbl', '.blg', '.lof', '.lot', '.fdb_latexmk', '.bak'}:
        return 'generated-or-backup-review'
    if ext in PROFILE: return 'raw-profile'
    if ext in SOURCE or p.name.lower() in {'cmakelists.txt', 'makefile'}: return 'source-script'
    if ext in DOC or p.name.lower().startswith(('readme', 'license', 'copying')): return 'document'
    if ext in DATA: return 'data-or-config'
    if ext in IMAGES: return 'figure-or-pdf'
    if ext in {'.stl', '.obj', '.ply', '.off', '.vtk', '.msh'}: return 'geometry'
    return 'other-review'

def api(path):
    url = 'https://api.github.com/' + path
    try:
        req = urllib.request.Request(url, headers={'User-Agent': '101lab-research-inventory', 'Accept': 'application/vnd.github+json'})
        with urllib.request.urlopen(req, timeout=30) as response:
            return {'url': url, 'ok': True, 'data': json.load(response)}
    except Exception as e:
        return {'url': url, 'ok': False, 'error': str(e)}

def dump(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2), encoding='utf-8')

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--roots', nargs='+', default=['D:/Stardis-GPU', 'D:/stardis-editor', 'D:/thesis'])
    parser.add_argument('--output', required=True)
    parser.add_argument('--remote', action='store_true')
    args = parser.parse_args()
    out = Path(args.output).resolve()
    out.mkdir(parents=True, exist_ok=True)
    package = Path(__file__).resolve().parents[1]
    rows, repos, skipped, errors = [], [], [], []
    groups = defaultdict(lambda: {'files': 0, 'bytes': 0, 'categories': defaultdict(int)})
    for root_arg in args.roots:
        root = Path(root_arg).resolve()
        def walk_error(err):
            errors.append({'path': err.filename, 'error': str(err)})
        for directory, dirs, files in os.walk(root, onerror=walk_error, followlinks=False):
            d = Path(directory)
            if (d / '.git').exists():
                commands = {'head': ['rev-parse', 'HEAD'], 'branch': ['branch', '--show-current'],
                            'status': ['status', '--short', '--branch'], 'remotes': ['remote', '-v'],
                            'branches': ['for-each-ref', '--format=%(refname:short)\t%(objectname)\t%(upstream:short)\t%(upstream:track)', 'refs/heads'],
                            'worktrees': ['worktree', 'list', '--porcelain'],
                            'last_commit': ['log', '-1', '--format=%H%n%aI%n%an%n%s'],
                            'authors': ['shortlog', '-s', '--all'],
                            'submodules': ['submodule', 'status'],
                            'diff_stat': ['diff', '--stat']}
                repos.append({'path': str(d), 'git_kind': 'worktree-or-submodule' if (d / '.git').is_file() else 'repository',
                              **{name: git(d, *command) for name, command in commands.items()}})
            for name in dirs[:]:
                child = d / name
                if name in SKIP or child.is_symlink() or child.resolve() == package or child.resolve() == out:
                    dirs.remove(name)
                    skipped.append(str(child))
            for name in files:
                p = d / name
                if name == '.git' or p.is_symlink(): continue
                try:
                    stat = p.stat()
                    rel = p.relative_to(root).as_posix()
                    group = rel.split('/')[0] if '/' in rel else '(root files)'
                    kind = category(rel)
                    row = {'root': str(root), 'path': rel, 'bytes': stat.st_size, 'category': kind,
                           'modified_utc': datetime.fromtimestamp(stat.st_mtime, timezone.utc).isoformat()}
                    rows.append(row)
                    g = groups[(str(root), group)]
                    g['files'] += 1
                    g['bytes'] += stat.st_size
                    g['categories'][kind] += 1
                except OSError as e:
                    errors.append({'path': str(p), 'error': str(e)})
    rows.sort(key=lambda r: (r['root'], r['path']))
    with (out / 'local-files.csv').open('w', encoding='utf-8-sig', newline='') as f:
        w = csv.DictWriter(f, fieldnames=['root', 'path', 'bytes', 'category', 'modified_utc'])
        w.writeheader(); w.writerows(rows)
    dump(out / 'local-summary.json', {'observed_at': datetime.now(timezone.utc).isoformat(), 'roots': args.roots,
        'scope': 'excludes git objects, virtualenvs, caches, node_modules and this report package; includes build outputs; sizes are not deduplicated',
        'files': len(rows), 'bytes': sum(x['bytes'] for x in rows),
        'groups': [{'root': r, 'directory': d, **v} for (r, d), v in sorted(groups.items())],
        'largest_files': sorted(rows, key=lambda x: x['bytes'], reverse=True)[:100],
        'over_100_mib': [x for x in rows if x['bytes'] > 100 * 1024**2], 'excluded_paths': skipped, 'errors': errors})
    dump(out / 'local-git.json', repos)
    if args.remote:
        remote = {'observed_at': datetime.now(timezone.utc).isoformat(), 'visibility': 'unauthenticated public API', 'accounts': {}, 'repositories': {}}
        for owner in ['101Laboratory', 'EricSolshkov']:
            pages, all_repos = [], []
            page = 1
            while True:
                result = api(f'users/{owner}/repos?per_page=100&type=owner&page={page}')
                pages.append(result)
                if not result['ok']: break
                all_repos.extend(result['data'])
                if len(result['data']) < 100: break
                page += 1
            remote['accounts'][owner] = {'complete': all(p['ok'] for p in pages), 'repositories': all_repos,
                                          'requests': [{k: v for k, v in p.items() if k != 'data'} for p in pages]}
        targets = ['EricSolshkov/' + name for name in ['stardis', 'stardis-cuda', 'stardis-editor', 'stardis-win', 'thesis', 'custar-3d', 'cuBQL', 'embree', 'sfa-mt-bridge']]
        for repo in targets:
            meta = api('repos/' + repo)
            item = {'metadata': meta}
            if meta['ok']:
                for key, endpoint in [('branches', 'branches?per_page=100'), ('tags', 'tags?per_page=100'), ('releases', 'releases?per_page=100'), ('root', 'contents')]:
                    item[key] = api('repos/' + repo + '/' + endpoint)
            remote['repositories'][repo] = item
        dump(out / 'remote-github.json', remote)
    print(json.dumps({'files': len(rows), 'gib': round(sum(x['bytes'] for x in rows) / 1024**3, 3), 'git_locations': len(repos), 'errors': len(errors)}, ensure_ascii=False))

if __name__ == '__main__':
    main()
