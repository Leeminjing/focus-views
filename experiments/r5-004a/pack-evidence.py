import argparse, concurrent.futures, gzip, hashlib, json, lzma, os, subprocess, tarfile
from pathlib import Path

def digest_file(p):
    h=hashlib.sha256()
    with p.open('rb') as f:
        while b:=f.read(1024*1024): h.update(b)
    return h.hexdigest()

def run(source,archive,prefix=None,exclude=None):
    source=Path(source).resolve(); archive=Path(archive).resolve()
    if archive.is_relative_to(source) or archive.exists(): raise RuntimeError('Preserve raw source and prior archives')
    roots=sorted(p for p in source.iterdir() if p.is_dir() and p.name not in exclude) if exclude else ([source] if prefix is None else sorted(p for p in source.iterdir() if p.is_dir() and p.name.startswith(prefix)))
    if not roots: raise RuntimeError('No evidence roots')
    files=sorted(p for root in roots for p in root.rglob('*') if p.is_file())
    if any(p.is_symlink() for p in files): raise RuntimeError('Unexpected reparse/symbolic evidence')
    with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:
        hashes=list(pool.map(digest_file,files))
    entries=[{'file':p.relative_to(source.parent).as_posix(),'bytes':p.stat().st_size,'sha256':h} for p,h in zip(files,hashes)]
    print('SOURCE_HASHED',source.name,len(entries),flush=True)
    archive.parent.mkdir(parents=True,exist_ok=True)
    subprocess.run(['tar.exe','--options','xz:compression-level=6,xz:threads=4','-cJf',str(archive),'-C',str(source.parent),*[root.relative_to(source.parent).as_posix() for root in roots]],check=True)
    wanted={e['file']:e for e in entries}; seen=set()
    with tarfile.open(archive,'r|xz') as stream:
        for member in stream:
            if member.isdir(): continue
            if not member.isfile() or member.name not in wanted or member.name in seen: raise RuntimeError('Unexpected/duplicate archive entry '+member.name)
            expected=wanted[member.name]
            if member.size!=expected['bytes']: raise RuntimeError('Length mismatch '+member.name)
            h=hashlib.sha256()
            with stream.extractfile(member) as f:
                while b:=f.read(1024*1024): h.update(b)
            if h.hexdigest()!=expected['sha256']: raise RuntimeError('Raw byte mismatch '+member.name)
            seen.add(member.name)
    if len(seen)!=len(wanted): raise RuntimeError('Missing archive members')
    manifest={'file':archive.name,'sha256':digest_file(archive),'bytes':archive.stat().st_size,'rawFiles':len(entries),'rawBytes':sum(e['bytes'] for e in entries),'everyEntryVerified':True,'verification':'streamed TAR extraction / every file length and SHA256; no duplicate disk tree','archiverSha256':digest_file(Path(__file__)),'entries':entries}
    text=json.dumps(manifest,ensure_ascii=False,indent=2).encode('utf-8')
    path=Path(str(archive)+'.manifest.json')
    if len(text)>32*1024*1024:
        path=Path(str(path)+'.gz')
        with gzip.open(path,'wb') as f:f.write(text)
    else:path.write_bytes(text)
    if archive.stat().st_size>32*1024*1024:
        parts=[]
        with archive.open('rb') as f:
            n=0
            while b:=f.read(32*1024*1024):
                n+=1;p=Path(str(archive)+f'.chunk-{n:02}');p.write_bytes(b);parts.append({'file':p.name,'bytes':len(b),'sha256':hashlib.sha256(b).hexdigest()})
        joined=hashlib.sha256()
        for p in parts:
            with archive.with_name(p['file']).open('rb') as f:
                while b:=f.read(1024*1024):joined.update(b)
        if joined.hexdigest()!=manifest['sha256']:raise RuntimeError('Transport concatenation mismatch')
        Path(str(archive)+'.parts.json').write_text(json.dumps({'file':archive.name,'bytes':manifest['bytes'],'sha256':manifest['sha256'],'parts':parts,'concatenatedSha256Verified':True},indent=2),encoding='utf-8')
    print('ARCHIVE_VERIFIED',archive,len(entries),manifest['bytes'],flush=True)

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('source');parser.add_argument('archive');parser.add_argument('--prefix');parser.add_argument('--exclude',action='append');a=parser.parse_args();run(a.source,a.archive,a.prefix,a.exclude)
