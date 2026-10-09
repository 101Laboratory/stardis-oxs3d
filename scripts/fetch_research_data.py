"""Download, checksum and restore versioned research archives. Python stdlib only."""
import argparse
import gzip
import hashlib
import io
import json
from pathlib import Path
import tarfile
import urllib.request

ROOT=Path(__file__).resolve().parents[1]

def sha(path):
    h=hashlib.sha256()
    with path.open('rb') as f:
        for b in iter(lambda:f.read(1024*1024),b''):h.update(b)
    return h.hexdigest()

class Joined:
    def __init__(self,paths):self.paths=iter(paths);self.f=None
    def read(self,n=-1):
        if n<0:raise ValueError('Only bounded streaming reads are supported')
        out=bytearray()
        while len(out)<n:
            if self.f is None:
                p=next(self.paths,None)
                if p is None:break
                self.f=p.open('rb')
            b=self.f.read(n-len(out))
            if b:out.extend(b)
            else:self.f.close();self.f=None
        return bytes(out)
    def close(self):
        if self.f:self.f.close();self.f=None

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--list',action='store_true')
    ap.add_argument('--dataset',action='append',default=[])
    ap.add_argument('--all',action='store_true')
    ap.add_argument('--local-parts',type=Path)
    ap.add_argument('--verify-only',action='store_true')
    ap.add_argument('--destination',type=Path,default=ROOT)
    a=ap.parse_args()
    manifest=json.loads((ROOT/'manifests/datasets.json').read_text(encoding='utf8'))
    records=json.loads((ROOT/'manifests/research-files.json').read_text(encoding='utf8'))
    lookup={r['published_path']:r for r in records if r['storage']=='release'}
    if a.list or not(a.dataset or a.all):
        for p in manifest['packages']:print(p['id'],p['files'],'files',sum(x['bytes'] for x in p['parts'])//1024**2,'MiB download')
        return
    names={p['id'] for p in manifest['packages']}
    if set(a.dataset)-names:ap.error('Unknown dataset: '+str(set(a.dataset)-names))
    cache=a.local_parts or ROOT/'data/.downloads';cache.mkdir(parents=True,exist_ok=True)
    dest=a.destination.resolve()
    for package in manifest['packages']:
        if not a.all and package['id'] not in a.dataset:continue
        paths=[]
        for part in package['parts']:
            path=cache/part['name']
            if not path.exists():
                if a.local_parts:raise FileNotFoundError(path)
                temp=path.with_suffix(path.suffix+'.partial')
                with urllib.request.urlopen(manifest['base_url']+part['name']) as r,temp.open('wb') as f:
                    while b:=r.read(1024*1024):f.write(b)
                if temp.stat().st_size!=part['bytes'] or sha(temp)!=part['sha256']:raise ValueError('Download checksum mismatch: '+part['name'])
                temp.replace(path)
            if path.stat().st_size!=part['bytes'] or sha(path)!=part['sha256']:raise ValueError('Archive checksum mismatch: '+part['name'])
            paths.append(path)
        joined=Joined(paths);count=0
        try:
            with gzip.GzipFile(fileobj=joined,mode='rb') as gz,tarfile.open(fileobj=gz,mode='r|') as archive:
                for member in archive:
                    if not member.isfile():raise ValueError('Unexpected archive member type: '+member.name)
                    target=(dest/member.name).resolve()
                    if not target.is_relative_to(dest):raise ValueError('Archive path escapes destination')
                    expected=lookup.get(member.name)
                    if not expected or expected['archive']!=package['id']:raise ValueError('Unexpected archive path')
                    if not a.verify_only and target.exists():
                        if sha(target)==expected['sha256']:
                            # Still consume and validate archive content below.
                            output=None
                        else:raise FileExistsError('Refusing to overwrite different existing file: '+str(target))
                    elif not a.verify_only:
                        target.parent.mkdir(parents=True,exist_ok=True);output=target.open('wb')
                    else:output=None
                    h=hashlib.sha256();size=0
                    try:
                        with archive.extractfile(member) as stream:
                            while b:=stream.read(1024*1024):
                                h.update(b);size+=len(b)
                                if output:output.write(b)
                    finally:
                        if output:output.close()
                    if h.hexdigest()!=expected['sha256'] or size!=expected['bytes']:raise ValueError('Member checksum mismatch: '+member.name)
                    count+=1
        finally:joined.close()
        if count!=package['files']:raise ValueError('Member count mismatch')
        print(package['id']+': verified '+str(count)+' files',flush=True)

if __name__=='__main__':main()
