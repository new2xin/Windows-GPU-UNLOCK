"""Preserve the post-release graph from the failed v5 physical boot for host tests."""
import argparse
import hashlib
import json
import re
import uuid
from pathlib import Path


def create(source, output):
    raw = source.read_bytes()
    if hashlib.sha256(raw).hexdigest() != '521024a508681071e89a59de2ce2ba534451103bcbf0f9db38ad34fd6a299ec8':
        raise ValueError('Unexpected physical failure capture')
    text = raw.decode('ascii').replace('\r\n', '\n')
    sections = text.split('gpu-console-graph-v1 observational-only; no-detach no-unlock no-nvram-write\n')
    if len(sections) != 3 or not sections[-1].endswith('handoff-stopped status=0x8000000000000006\n'):
        raise ValueError('Expected two complete capture sections and the recorded child failure')
    text = sections[-1]
    if text.count('capture-complete=1\n') != 1:
        raise ValueError('Post-release capture incomplete')
    blocks = dict(re.findall(r'^handle id=(0x[0-9a-f]+)\n(.*?)(?=^handle id=|^capture-complete=)', text, re.M | re.S))
    gpu = [h for h, b in blocks.items() if 'pci config-dwords=220810de' in b]
    console = re.search(r'^console out=(0x[0-9a-f]+) error=(0x[0-9a-f]+) input=(0x[0-9a-f]+)$', text, re.M)
    if len(gpu) != 1 or not console or len(set(console.groups())) != 1:
        raise ValueError('Unexpected target or console roles')
    ids = {h: i + 1024 for i, h in enumerate(blocks)}
    ids[gpu[0]] = 3
    ids[console[1]] = 6
    edges = []
    for h, block in blocks.items():
        protocol = None
        for line in block.splitlines():
            if line.startswith('protocol guid='):
                protocol = uuid.UUID(line[14:])
            m = re.fullmatch(r'open agent=(0x[0-9a-f]+) controller=(0x[0-9a-f]+) attributes=(0x[0-9a-f]+) count=(0x[0-9a-f]+)', line)
            if m and int(m[3], 16) not in (1, 2, 4):
                if protocol is None or m[1] not in ids or m[2] not in ids:
                    raise ValueError('Unresolved active edge')
                edges.append((ids[h], ids[m[1]], ids[m[2]], protocol, int(m[3], 16), int(m[4], 16)))
    if any(e[0] == 3 for e in edges):
        raise ValueError('GPU driver/child remained after reported release')
    lines = ['/* Host-only post-release physical graph; addresses mapped to fixture roles. */',
             'static const GU_HEADLESS_EDGE MsiCapturedEdges[]={']
    for h, a, c, g, attrs, count in edges:
        guid = f'{{0x{g.time_low:x},0x{g.time_mid:x},0x{g.time_hi_version:x},{{' + ','.join(f'0x{x:x}' for x in g.bytes[8:]) + '}}'
        lines.append(f'{{H({h}),H({a}),H({c}),{guid},{attrs},{count}}},')
    lines.append('};')
    (output / 'child-replay-fixture.h').write_text('\n'.join(lines) + '\n', encoding='ascii')
    evidence = dict(source=str(source.resolve()), sha256=hashlib.sha256(raw).hexdigest(),
                    handles=len(blocks), active_edges=len(edges), gpu_active_edges=0, hardware_access=False)
    (output / 'child-replay-provenance.json').write_text(json.dumps(evidence, indent=2) + '\n', encoding='utf-8')
    print(f'Post-release physical graph: {len(blocks)} handles, {len(edges)} active edges')


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('source', type=Path)
    p.add_argument('output', type=Path)
    a = p.parse_args()
    create(a.source, a.output)
