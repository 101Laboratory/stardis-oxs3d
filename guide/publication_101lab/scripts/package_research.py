"""Copy research source/text and archive large data in checksum-verified split gzip tar volumes."""
import csv
import gzip
import hashlib
import io
import json
from pathlib import Path
import shutil
import tarfile

ROOT=Path('D:/Stardis-GPU')
BASE=Path('D:/Stardis-Publication-101Lab')
CORE=BASE/'repos/stardis-oxs3d'
ASSETS=BASE/'assets'
GROUPS={'guide','debug_issues','optimization','GPU_WF_Validation','Stardis-Starter-Pack','cuda-duplex-validation','optix-throughput-validation','perf_diag','physical_consistency_stats','profiling','scripts'}
SKIP_DIR={'.git','.venv','.vs','.vscode','__pycache__','.pytest_cache','node_modules','build','build_test','build_s3d'}
SKIP_EXT={'.obj','.pdb','.ilk','.exp','.exe','.dll','.lib','.pyc','.ipch'}

class SplitWriter:
    def __init__(self,name):self.name=name;self.index=0;self.file=None;self.size=0;self.parts=[];self.sha=None
    def new(self):
        self.close_part();self.index+=1;self.size=0;self.sha=hashlib.sha256()
        self.path=ASSETS/f'{self.name}.tar.gz.part{self.index:03d}';self.file=self.path.open('wb')
    def write(self,data):
        view=memoryview(data);total=len(view)
        while view:
            if self.file is None or self.size>=512*1024**2:self.new()
            n=min(len(view),512*1024**2-self.size);chunk=view[:n]
            self.file.write(chunk);self.sha.update(chunk);self.size+=n;view=view[n:]
        return total
    def flush(self):
        if self.file:self.file.flush()
    def close_part(self):
        if self.file:
            self.file.close();self.parts.append({'name':self.path.name,'bytes':self.size,'sha256':self.sha.hexdigest()});self.file=None

class HashReader:
    def __init__(self,file):self.file=file;self.sha=hashlib.sha256()
    def read(self,n=-1):
        b=self.file.read(n);self.sha.update(b);return b

def main():
    ASSETS.mkdir(exist_ok=True)
    rows=[];archives={};copied=[];excluded=[]
    with (ROOT/'guide/publication_101lab/evidence/local-files.csv').open(encoding='utf-8-sig',newline='') as f:inventory=list(csv.DictReader(f))
    for r in inventory:
        root=Path(r['root']);rel=Path(r['path']);src=root/rel
        if root==ROOT:
            if rel.parts[0] not in GROUPS:continue
            dst=rel;group=rel.parts[0]
        elif root==Path('D:/thesis') and rel.parts[0] in {'OurWork','References','ref_need','report','ReviewComments','Figures - bak'}:
            dst=Path('thesis-materials')/rel;group='thesis-materials'
        else:continue
        if any(p in SKIP_DIR or p.startswith(('build_','build-')) for p in rel.parts) or src.name=='CHANGE_LOG':
            excluded.append({'path':str(src),'reason':'build/cache/live-log'});continue
        # Full profiler sessions contain symbol binaries that belong to the archived session.
        if group not in {'profiling'} and src.suffix.lower() in SKIP_EXT:
            if not (src.suffix.lower()=='.obj' and '.scene_validation' in src.name):
                excluded.append({'path':str(src),'reason':'rebuildable-binary'});continue
        record={'source_root':str(root),'source_path':rel.as_posix(),'published_path':dst.as_posix(),'bytes':src.stat().st_size}
        large=(group=='profiling' or record['bytes']>=20*1024**2 or src.suffix.lower() in {'.ht','.ppm','.ncu-rep','.nsys-rep','.diagsession','.etl'} or 'Figures - bak' in rel.parts)
        if large:
            record['storage']='release';record['archive']=group
            archives.setdefault(group,[]).append((src,dst,record))
        else:
            record['storage']='git';target=CORE/dst;target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(src,target)
            record['sha256']=hashlib.sha256(src.read_bytes()).hexdigest();rows.append(record);copied.append(dst.as_posix())
    packages=[]
    for group,items in archives.items():
        writer=SplitWriter(group)
        with gzip.GzipFile(fileobj=writer,mode='wb',compresslevel=1,mtime=0) as gz:
            with tarfile.open(fileobj=gz,mode='w|') as tar:
                for src,dst,record in items:
                    info=tar.gettarinfo(str(src),arcname=dst.as_posix())
                    with src.open('rb') as f:
                        reader=HashReader(f);tar.addfile(info,reader);record['sha256']=reader.sha.hexdigest()
                    rows.append(record)
        writer.close_part();packages.append({'id':group,'format':'concatenated gzip tar volumes','parts':writer.parts,'files':len(items),'raw_bytes':sum(r['bytes'] for _,_,r in items)})
        print(json.dumps({'archived':group,'files':len(items),'parts':len(writer.parts),'compressed_bytes':sum(p['bytes'] for p in writer.parts)}),flush=True)
    manifests=CORE/'manifests';manifests.mkdir(exist_ok=True)
    (manifests/'research-files.json').write_text(json.dumps(rows,ensure_ascii=False,indent=2),encoding='utf8')
    (manifests/'datasets.json').write_text(json.dumps({'release_tag':'research-archive-2026-10-09','base_url':'https://github.com/101Laboratory/stardis-oxs3d/releases/download/research-archive-2026-10-09/','packages':packages},ensure_ascii=False,indent=2),encoding='utf8')
    (BASE/'research-excluded.json').write_text(json.dumps(excluded,ensure_ascii=False,indent=2),encoding='utf8')
    (BASE/'research-git-paths.json').write_text(json.dumps(copied,ensure_ascii=False),encoding='utf8')
    print(json.dumps({'git_files':len(copied),'archived_files':sum(x['files'] for x in packages),'excluded_generated':len(excluded)}),flush=True)

if __name__=='__main__':main()
