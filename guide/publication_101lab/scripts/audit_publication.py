"""Check staged publication files and existing Git history for obvious credentials/oversize blobs."""
import json
from pathlib import Path
import re
import subprocess

BASE=Path('D:/Stardis-Publication-101Lab')
PATTERNS={
 'github_token':rb'\b(?:gh[pousr]_[A-Za-z0-9]{36,255}|github_pat_[A-Za-z0-9_]{70,255})\b',
 'private_key':rb'-----BEGIN (?:RSA |EC |OPENSSH |DSA )?PRIVATE KEY-----',
 'aws_access_key':rb'\bAKIA[A-Z0-9]{16}\b',
 'slack_token':rb'\bxox[baprs]-[0-9A-Za-z-]{20,}\b',
}
PATTERNS={k:re.compile(v) for k,v in PATTERNS.items()}

def scan(data,where,findings):
    for name,pattern in PATTERNS.items():
        if pattern.search(data):findings.append({'location':where,'pattern':name})

def main():
    findings=[];oversize=[];files=0;blobs=0
    for repo in (BASE/'repos').iterdir():
        for p in repo.rglob('*'):
            if not p.is_file() or '.git' in p.parts or '__pycache__' in p.parts or p.suffix in {'.pdf','.xdv','.png','.jpg','.ppm','.stl'}:continue
            if p.stat().st_size<20*1024**2:scan(p.read_bytes(),str(p.relative_to(BASE)),findings);files+=1
        r=subprocess.run(['git','-C',str(repo),'rev-list','--objects','--all'],capture_output=True,check=True)
        names=dict(l.split(b' ',1) if b' ' in l else (l,b'') for l in r.stdout.splitlines())
        if not names:continue
        checked=subprocess.run(['git','-C',str(repo),'cat-file','--batch-check=%(objectname) %(objecttype) %(objectsize)'],input=b'\n'.join(names)+b'\n',capture_output=True,check=True)
        selected=[]
        for line in checked.stdout.splitlines():
            sha,kind,size=line.split();size=int(size)
            if kind!=b'blob':continue
            if size>100*1024**2:oversize.append({'repo':repo.name,'sha':sha.decode(),'bytes':size,'path':names[sha].decode('utf8','replace')})
            if size<20*1024**2:selected.append(sha)
        # communicate drains stdout while supplying stdin, avoiding pipe backpressure.
        result=subprocess.run(['git','-C',str(repo),'cat-file','--batch'],input=b'\n'.join(selected)+b'\n',capture_output=True,check=True)
        pos=0
        for sha in selected:
            end=result.stdout.index(b'\n',pos);header=result.stdout[pos:end].split();size=int(header[2]);pos=end+1
            scan(result.stdout[pos:pos+size],repo.name+':'+sha.decode()+':'+names[sha].decode('utf8','replace'),findings)
            pos+=size+1;blobs+=1
    report={'scope':'Common credential patterns only; current text-like files and reachable history blobs under 20 MiB; no guarantee of exhaustive detection','current_files':files,'history_blobs':blobs,'findings':findings,'history_blobs_over_100_mib':oversize}
    (BASE/'publication-audit.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
    print(json.dumps(report,ensure_ascii=False))
    if findings or oversize:raise SystemExit(2)

if __name__=='__main__':main()
