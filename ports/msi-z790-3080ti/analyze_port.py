"""Validate private observer output and extract a non-arming profile candidate."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct


def digest(data):
    return hashlib.sha256(data).hexdigest()


def read_private(root, name, limit):
    path = root / name
    if path.is_symlink() or path.parent.resolve() != root or not path.is_file():
        raise ValueError(f'Unsafe or missing capture file: {name}')
    if path.stat().st_size > limit:
        raise ValueError(f'Oversized capture file: {name}')
    return path.read_bytes()


def image_sections(raw):
    if len(raw) < 256 or raw[:2] != b'MZ':
        raise ValueError('Not a mapped PE image')
    pe = struct.unpack_from('<I', raw, 0x3c)[0]
    if pe > len(raw) - 24 or raw[pe:pe+4] != b'PE\0\0':
        raise ValueError('Invalid PE signature')
    machine, sections = struct.unpack_from('<HH', raw, pe+4)
    optional = struct.unpack_from('<H', raw, pe+20)[0]
    if machine != 0x8664 or not 1 <= sections <= 96:
        raise ValueError('Unsupported PE image')
    table = pe+24+optional
    if table + 40*sections > len(raw):
        raise ValueError('Truncated PE section table')
    result = []
    for i in range(sections):
        at = table+40*i
        name = raw[at:at+8].rstrip(b'\0').decode('ascii', 'replace')
        length, rva = struct.unpack_from('<II', raw, at+8)
        if rva > len(raw) or length > len(raw)-rva:
            raise ValueError('Mapped section outside image')
        if name == '.text':
            result.append(dict(name=name, rva=rva, bytes=length, sha256=digest(raw[rva:rva+length])))
    if len(result) != 1:
        raise ValueError('Missing or ambiguous .text section')
    return result


def analyze(directory):
    root = Path(directory).resolve(strict=True)
    raw = read_private(root, 'CAPTURE.TXT', 8*1024*1024)
    text = raw.decode('utf-8-sig').replace('\r\n', '\n')
    if text.count('capture-complete=1\n') != 1 or 'capture-complete=0' in text or 'incomplete reason=' in text:
        raise ValueError('Incomplete or ambiguous capture; keep it for manual diagnosis')
    if text.count('port-observation-only=1 can-unlock=0\n') != 1:
        raise ValueError('Missing observer completion marker')
    def one(pattern):
        values = re.findall(pattern, text, re.M)
        if len(values) != 1:
            raise ValueError(f'Missing or ambiguous field: {pattern}')
        return values[0]
    if int(one(r'^port-target-count=0x([0-9a-f]+)$'), 16) != 1:
        raise ValueError('Target must be unique')
    location = tuple(int(v, 16) for v in one(r'^port-location seg=0x([0-9a-f]+) bus=0x([0-9a-f]+) dev=0x([0-9a-f]+) fun=0x([0-9a-f]+)$'))
    if location != (0, 1, 0, 0):
        raise ValueError('Unexpected target BDF')
    cfg = [int(v, 16) for v in one(r'^port-config-dwords=([0-9a-f ]+)$').split()]
    if len(cfg) != 64 or cfg[0] != 0x220810de or cfg[11] != 0x22081569:
        raise ValueError('Unexpected PCI identity')
    registers = {}
    for off, status, value in re.findall(r'^port-mmio off=0x([0-9a-f]+) status=0x([0-9a-f]+) value=0x([0-9a-f]+)$', text, re.M):
        if off in registers or int(status, 16):
            raise ValueError('Duplicate or failed register read')
        registers[off] = int(value, 16)
    required = [0, 0x1183a4, 0x1fa7c8, 0x1fa828, 0x1fa82c, 0x1fa830, 0x625f04]
    if any(f'{off:08x}' not in registers for off in required) or registers['00000000'] != 0xb72000a1:
        raise ValueError('Incomplete GA102 layout inputs')
    prom = read_private(root, 'PROM.BIN', 0x100000)
    prom_hash = one(r'^port-prom-sha256=([0-9a-f]{64})$')
    if len(prom) != 0x100000 or digest(prom) != prom_hash or len(set(prom)) == 1:
        raise ValueError('PROM digest, size or content mismatch')
    entries = re.findall(r'^port-image index=0x([0-9a-f]+) handle=0x([0-9a-f]+) base=0x([0-9a-f]+) bytes=0x([0-9a-f]+)\nport-image sha256=([0-9a-f]{64})$', text, re.M)
    declared = int(one(r'^port-image-count=0x([0-9a-f]+)$'), 16)
    declared_bytes = int(one(r'^port-image-total-bytes=0x([0-9a-f]+)$'), 16)
    if not entries or len(entries) != declared or declared > 96:
        raise ValueError('Incomplete image set')
    images = []
    for i, (index, handle, base, size, sha) in enumerate(entries):
        if int(index, 16) != i:
            raise ValueError('Non-sequential image indices')
        data = read_private(root, f'IMG{i:03d}.BIN', 4*1024*1024)
        if len(data) != int(size, 16) or digest(data) != sha:
            raise ValueError('Driver image digest or size mismatch')
        image = dict(index=i, handle=handle, base=int(base, 16), bytes=len(data), sha256=sha)
        try:
            image['sections'] = image_sections(data)
        except ValueError as error:
            image['parse_error'] = str(error)
        stops = re.findall(r'^port-binding handle=0x[0-9a-f]+ image=0x'+re.escape(handle)+r' version=0x([0-9a-f]+) stop=0x([0-9a-f]+)$', text, re.M)
        image['bindings'] = [dict(version=int(v, 16), stop_rva=int(s, 16)-int(base, 16)) for v, s in stops]
        images.append(image)
    if sum(i['bytes'] for i in images) != declared_bytes or declared_bytes > 32*1024*1024:
        raise ValueError('Image budget mismatch')
    return dict(schema_version=1, purpose='private-port-evidence', status='requires-review', can_unlock=False,
                capture_sha256=digest(raw), pci_config=cfg, location=location,
                bar0_base=int(one(r'^port-bar0-base=0x([0-9a-f]+)$'), 16),
                bar0_bytes=int(one(r'^port-bar0-length=0x([0-9a-f]+)$'), 16),
                registers=registers, prom_sha256=prom_hash, images=images,
                missing_for_write_profile=['reviewed console ownership and detach/failure policy',
                    'reviewed PROM/firmware compatibility and VRAM/WPR layout',
                    'physical no-reset Windows handoff and recovery evidence'])


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory')
    parser.add_argument('--output', required=True)
    args = parser.parse_args()
    result = analyze(args.directory)
    with Path(args.output).open('x', encoding='utf-8') as stream:
        json.dump(result, stream, indent=2)
        stream.write('\n')
    print('Evidence validated; write eligibility remains disabled.')
