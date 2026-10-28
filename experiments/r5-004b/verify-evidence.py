"""Read-only transport verification; this does not execute a native experiment."""
import argparse
import hashlib
import io
import json
import tarfile
from pathlib import Path


def digest(path):
    result = hashlib.sha256()
    with path.open('rb') as stream:
        while data := stream.read(1024 * 1024):
            result.update(data)
    return result.hexdigest()


class Parts(io.RawIOBase):
    def __init__(self, paths):
        self.paths = iter(paths)
        self.current = None

    def readable(self):
        return True

    def readinto(self, buffer):
        while True:
            if self.current is None:
                try:
                    self.current = next(self.paths).open('rb')
                except StopIteration:
                    return 0
            count = self.current.readinto(buffer)
            if count:
                return count
            self.current.close()
            self.current = None

    def close(self):
        if self.current is not None:
            self.current.close()
        super().close()


def verify(manifest_path, deep):
    manifest = json.loads(manifest_path.read_text(encoding='utf-8'))
    root = manifest_path.parent
    archive = root / manifest['file']
    if archive.is_file():
        paths = [archive]
    else:
        parts = json.loads(Path(str(archive) + '.parts.json').read_text(encoding='utf-8'))
        assert parts['sha256'] == manifest['sha256']
        assert parts['bytes'] == manifest['bytes']
        paths = []
        for part in parts['parts']:
            assert Path(part['file']).name == part['file']
            path = root / part['file']
            assert path.stat().st_size == part['bytes'] and digest(path) == part['sha256'], path
            paths.append(path)
    total = hashlib.sha256()
    for path in paths:
        with path.open('rb') as stream:
            while data := stream.read(1024 * 1024):
                total.update(data)
    assert sum(path.stat().st_size for path in paths) == manifest['bytes']
    assert total.hexdigest() == manifest['sha256'], archive
    if deep:
        expected = {item['file']: item for item in manifest['entries']}
        assert len(expected) == manifest['rawFiles'] == len(manifest['entries'])
        seen = set()
        with Parts(paths) as raw, io.BufferedReader(raw) as stream, tarfile.open(fileobj=stream, mode='r|xz') as tar:
            for member in tar:
                if member.isdir():
                    continue
                assert member.isfile() and member.name in expected and member.name not in seen, member.name
                assert not member.name.lower().endswith('.dmp'), 'Private dump must not be published'
                item = expected[member.name]
                assert member.size == item['bytes']
                value = hashlib.sha256()
                with tar.extractfile(member) as file:
                    while data := file.read(1024 * 1024):
                        value.update(data)
                assert value.hexdigest() == item['sha256'], member.name
                seen.add(member.name)
        assert seen == expected.keys()
    return dict(file=manifest['file'], rawFiles=manifest['rawFiles'], passed=True, deep=deep)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('root', nargs='?', type=Path, default=Path(__file__).parent / 'results/r5004b')
    parser.add_argument('--deep', action='store_true')
    args = parser.parse_args()
    rows = [verify(path, args.deep) for path in sorted(args.root.glob('*.tar.xz.manifest.json'))]
    assert rows, 'No evidence archives'
    print(json.dumps(dict(passed=True, nativeExperiments=0, archives=rows), indent=2))
