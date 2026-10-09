"""Prepare isolated publication repositories. Original research roots remain unchanged."""
import csv
import json
import os
from pathlib import Path
import shutil
import subprocess

SOURCE=Path('D:/Stardis-GPU')
BASE=Path('D:/Stardis-Publication-101Lab')
REPOS=BASE/'repos'
ENV={**os.environ,'GIT_TERMINAL_PROMPT':'0','GCM_INTERACTIVE':'never','GIT_LFS_SKIP_SMUDGE':'1'}
_safe=[str(p)+suffix for p in ['D:/Stardis-GPU/stardis-oxs3d','D:/Stardis-GPU/stardis-cpu','D:/stardis-editor'] for suffix in ['', '/.git']]
ENV['GIT_CONFIG_COUNT']=str(len(_safe))
for _i,_path in enumerate(_safe):
    ENV[f'GIT_CONFIG_KEY_{_i}']='safe.directory'
    ENV[f'GIT_CONFIG_VALUE_{_i}']=_path

def git(p,*args):
    r=subprocess.run(['git','-c','safe.directory='+str(p).replace('\\','/'),'-c','credential.username=EricSolshkov','-C',str(p),*args],capture_output=True,env=ENV)
    if r.returncode:raise RuntimeError('git '+str(args[:3])+': '+r.stderr.decode('utf8','replace')[-1800:])
    return r.stdout

def textgit(p,*args):return git(p,*args).decode('utf8','replace').strip()

def identity(p):
    for key,value in [('user.name','ericpu'),('user.email','eric_pu@foxmail.com'),('credential.username','EricSolshkov')]:git(p,'config',key,value)

def clone(src,name):
    p=REPOS/name
    if p.exists():raise RuntimeError('Refusing to overwrite existing '+str(p))
    r=subprocess.run(['git','clone','--no-hardlinks',src.as_posix(),p.as_posix()],capture_output=True,env=ENV)
    if r.returncode:raise RuntimeError(r.stderr.decode('utf8','replace'))
    identity(p)
    for line in textgit(src,'for-each-ref','--format=%(refname:short) %(objectname)','refs/heads').splitlines():
        name,sha=line.split()
        if name!=textgit(p,'branch','--show-current'):git(p,'branch',name,sha)
    return p

def excluded(rel):
    parts=Path(rel).parts
    return any(x in {'.git','.venv','__pycache__','.pytest_cache','.vs','.vscode'} or x.startswith(('build_','build-')) or x=='build' for x in parts)

def snapshot(dst,src,branch):
    git(dst,'checkout',branch)
    paths=set(os.fsdecode(x) for x in git(src,'diff','HEAD','--name-only','-z').split(b'\0') if x)
    paths.update(os.fsdecode(x) for x in git(src,'ls-files','--others','--exclude-standard','-z').split(b'\0') if x)
    copied=[]
    for rel in sorted(paths):
        if excluded(rel):continue
        a,b=src/rel,dst/rel
        if a.is_file():
            b.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(a,b);copied.append(rel)
        elif not a.exists() and b.is_file():
            if not b.resolve().is_relative_to(dst.resolve()):raise RuntimeError('Unsafe snapshot path')
            b.unlink();copied.append(rel)
    if copied:
        git(dst,'add','--',*copied)
        if textgit(dst,'diff','--cached','--stat'):git(dst,'commit','-m','archive: preserve local research snapshot for '+branch)
    return {'source':str(src),'branch':branch,'source_head':textgit(src,'rev-parse','HEAD'),'snapshot_head':textgit(dst,'rev-parse','HEAD'),'files':copied}

def main():
    REPOS.mkdir(parents=True,exist_ok=True)
    records=[]
    gpu=clone(SOURCE/'stardis-oxs3d','stardis-oxs3d')
    for row in json.loads((SOURCE/'guide/publication_101lab/evidence/local-git.json').read_text(encoding='utf8')):
        src=Path(row['path'])
        if src.name.startswith('stardis-oxs3d'):
            records.append(snapshot(gpu,src,row['branch']['text']))
    git(gpu,'checkout','main')
    for i,line in enumerate(textgit(SOURCE/'stardis-oxs3d','stash','list','--format=%H').splitlines()):
        ref=f'archive/stash-{i}'
        git(gpu,'fetch',str(SOURCE/'stardis-oxs3d'),line+':refs/heads/'+ref)
    git(gpu,'fetch',str(SOURCE/'stardis-cpu'),'refs/heads/master:refs/heads/archive/cpu-windows')
    git(gpu,'subtree','add','--prefix=baselines/stardis-cpu','archive/cpu-windows','-m','archive: include Windows CPU baseline with existing parent history')
    editor=clone(Path('D:/stardis-editor'),'stardis-editor')
    records.append(snapshot(editor,Path('D:/stardis-editor'),'master'))
    thesis=REPOS/'stardis-thesis';thesis.mkdir()
    with (SOURCE/'guide/publication_101lab/evidence/thesis-source-scope.csv').open(encoding='utf-8-sig',newline='') as f:
        for row in csv.DictReader(f):
            if not row['destination'].startswith('latex-source'):continue
            a=Path('D:/thesis')/row['path'];b=thesis/row['path']
            b.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(a,b)
    # Template is deliberately vendored, including its uncommitted bibliography style.
    modules=thesis/'.gitmodules'
    if modules.exists():modules.unlink()
    git(thesis,'init','-b','main');identity(thesis)
    hub=REPOS/'stardis-research';hub.mkdir();git(hub,'init','-b','main');identity(hub)
    (BASE/'snapshot-records.json').write_text(json.dumps(records,ensure_ascii=False,indent=2),encoding='utf8')
    print(json.dumps({'stage':str(REPOS),'snapshots':len(records),'source_repositories_modified':False}))

if __name__=='__main__':main()
