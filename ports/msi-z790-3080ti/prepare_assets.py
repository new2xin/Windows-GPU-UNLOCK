"""Extract hash-pinned firmware with an existing cached zstandard module.

The upstream scripts and input archives must already be in --reference. Only
new directories are accepted. Archive members are read to RAM, never executed.
"""
import argparse
import hashlib
import importlib.util
import json
import sys
import tarfile
from pathlib import Path


def sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def load(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def prepare(reference, output):
    reference = reference.resolve(strict=True)
    manifest = json.loads((reference / 'artifacts-610.43.03.json').read_text())
    archives = {}
    for entry in (manifest['source_artifacts'][0], manifest['source_artifacts'][2]):
        path = reference / 'inputs' / entry['filename']
        if sha(path) != entry['sha256']:
            raise ValueError('Input archive SHA-256 mismatch: ' + path.name)
        archives[path.name] = entry['sha256']
    source, package = [reference / 'inputs' / e['filename'] for e in
                       (manifest['source_artifacts'][0], manifest['source_artifacts'][2])]
    def member(archive, name):
        archive = Path(archive)
        if archive.name.endswith('.zst'):
            import zstandard
            with archive.open('rb') as compressed, zstandard.ZstdDecompressor().stream_reader(compressed) as stream:
                with tarfile.open(fileobj=stream, mode='r|') as entries:
                    result = generator.read_member(entries, name)
        else:
            with tarfile.open(archive, mode='r:*') as entries:
                result = generator.read_member(entries, name)
        return result

    output.mkdir(parents=True, exist_ok=False)
    # Keep the pinned extractor unchanged; replace only its zstd subprocess
    # reader. Final bytes have independent pinned hashes.
    generator = load(reference / 'generate_v67.py', 'pinned_v67')
    generator.extract_member = member
    previous = sys.argv
    try:
        sys.argv = ['generate_v67.py', str(package), str(output)]
        if generator.main() != 0:
            raise ValueError('Pinned payload generator failed')
    finally:
        sys.argv = previous
    extractor = load(reference / 'extract_boot_assets.py', 'pinned_boot')
    root = output / 'pinned-source'
    for name, expected in extractor.PINNED.items():
        data = member(source, 'NVIDIA-kernel-module-source-610.43.03/src/nvidia/generated/' + name)
        if hashlib.sha256(data).hexdigest() != expected:
            raise ValueError('Generated C source mismatch')
        target = root / 'src/nvidia/generated' / name
        target.parent.mkdir(parents=True, exist_ok=True)
        with target.open('xb') as stream:
            stream.write(data)
    extractor.extract_assets(root, output)
    required = json.loads((Path(__file__).parents[2] / 'internal-boot/unlock/assets.json').read_text())
    # Record all outputs, with final hashes checked below by the local manifest.
    files = {p.name: {'bytes': p.stat().st_size, 'sha256': sha(p)}
             for p in output.iterdir() if p.is_file()}
    pinned = dict(manifest['derived_assets'])
    pinned.update({'gsp_ga10x.bin': generator.GSP_SHA256,
                   'v67-ga102-610.43.03.bin': generator.PAYLOAD_SHA256,
                   'NVIDIA-LICENSE': manifest['source_artifacts'][2]['notice']['sha256']})
    for name, expected in pinned.items():
        if files[name]['sha256'] != expected:
            raise ValueError('Final asset SHA-256 mismatch: ' + name)
    record = dict(reference_revision='0ebdb1c6308b6a2072be5bf04513472942541956',
                  archives=archives, files=files, installed=False, can_unlock=False,
                  local_asset_requirements=required,
                  tools={p.name: sha(p) for p in reference.iterdir() if p.is_file()})
    with (output / 'provenance.json').open('x', encoding='utf-8') as stream:
        json.dump(record, stream, indent=2)
        stream.write('\n')
    print(json.dumps(files, indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reference', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--zstd-module-root', type=Path)
    args = parser.parse_args()
    if args.zstd_module_root:
        sys.path.insert(0, str(args.zstd_module_root.resolve(strict=True)))
    prepare(args.reference, args.output)
