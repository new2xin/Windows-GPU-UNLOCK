"""Create private host fixtures from a fully verified real diagnostic.

No hardware access. All captured EFI addresses become ordinal handles.
"""
import argparse
import json
import re
import uuid
from pathlib import Path
from analyze_port import analyze


def create(root, output):
    evidence = analyze(root)
    text = (root / 'CAPTURE.TXT').read_text(encoding='utf-8-sig')
    blocks = dict(re.findall(r'^handle id=(0x[0-9a-f]+)\n(.*?)(?=^handle id=|^capture-complete=|\Z)', text, re.M | re.S))
    indices = {h: i+1 for i, h in enumerate(blocks)}
    edges = []
    for handle, block in blocks.items():
        protocol = None
        for line in block.splitlines():
            if line.startswith('protocol guid='):
                protocol = uuid.UUID(line[14:])
            match = re.fullmatch(r'open agent=(0x[0-9a-f]+) controller=(0x[0-9a-f]+) attributes=(0x[0-9a-f]+) count=(0x[0-9a-f]+)', line)
            if match and int(match[3], 16) not in (1, 2, 4):
                if protocol is None or match[1] not in indices or match[2] not in indices:
                    raise ValueError('Unresolved active ownership reference')
                edges.append((handle, match[1], match[2], protocol, int(match[3],16), int(match[4],16)))

    def one(values):
        if len(values) != 1:
            raise ValueError('Expected unique role: ' + str(values))
        return values[0]

    def path(block, prefix='path-node'):
        return b''.join(bytes.fromhex(x) for x in re.findall(r'^'+prefix+r' raw=([0-9a-f]+)$', block, re.M))

    roles = {'RtxPci': one([h for h,b in blocks.items() if 'pci config-dwords=220810de' in b]),
             'Rtx': one([h for h,b in blocks.items() if 'gop framebuffer=0x0000006000000000' in b])}
    console = re.search(r'^console out=(0x[0-9a-f]+) error=(0x[0-9a-f]+) input=(0x[0-9a-f]+)$', text, re.M)
    roles.update(dict(zip(('Console','Error','Input'), console.groups())))
    roles['UsbHost'] = one([h for h,b in blocks.items() if 'pci config-dwords=21421b21' in b])
    roles['UsbAgent'] = one([e[1] for e in edges if e[0]==roles['UsbHost'] and str(e[3])=='3e745226-9818-45b6-a2ac-d7cd0e8ba2bc' and e[4]==16])
    roles['UsbImage'] = re.search(r'^driver image=(0x[0-9a-f]+)', blocks[roles['UsbAgent']], re.M)[1]
    usb_prefix = bytes.fromhex('02010c00d041030a0000000001010600011c010106000000')
    usb_paths = [usb_prefix+bytes.fromhex(s) for s in ('0305060000007fff0400','0305060002007fff0400','0305060002000305060001007fff0400')]
    for i,p in enumerate(usb_paths):
        roles[f'UsbLeaf{i}'] = one([e[2] for e in edges if e[0]==roles['UsbHost'] and e[4]==8 and path(blocks[e[2]])==p])
    roles['UsbOtherChild'] = one([e[2] for e in edges if e[0]==roles['UsbHost'] and e[4]==8 and e[2] not in [roles[f'UsbLeaf{i}'] for i in range(3)]])
    roles['OtherChild'] = one([e[2] for e in edges if e[0]==roles['RtxPci'] and e[4]==8 and e[2]!=roles['Rtx']])
    for role, guid, controller in [('Out', '387477c2-69c7-11d2-8e39-00a0c969723b', roles['Rtx']),
                                   ('Gfx', '9042a9de-23dc-4a38-96fb-7aded080516a', roles['Rtx']),
                                   ('Nv', '4cf5b200-68b8-4ca5-9eec-b23e3f50029a', roles['RtxPci'])]:
        agent = one([e[1] for e in edges if e[0]==controller and str(e[3])==guid and e[4]==16])
        roles[role+'Agent'] = agent
        fields = re.search(r'^driver image=(0x[0-9a-f]+) binding=(0x[0-9a-f]+) version=0x([0-9a-f]+) stop-address=0x([0-9a-f]+)$', blocks[agent], re.M)
        if fields[2] != agent:
            raise ValueError('Mismatched binding')
        image = fields[1];roles[role+'Image'] = image
    lines = [f'/* Strict capture SHA-256: {evidence["capture_sha256"]} */', f'#define FX_HANDLES {len(blocks)}']
    for role, handle in roles.items():
        lines.append(f'#define FX_{role} ((EFI_HANDLE){indices[handle]})')
    lines.append('static const EDGE FixtureEdges[]={')
    for h,a,c,g,attrs,count in edges:
        guid = f'{{0x{g.time_low:x},0x{g.time_mid:x},0x{g.time_hi_version:x},{{'+','.join(f'0x{x:x}' for x in g.bytes[8:])+'}}'
        lines.append(f'{{(EFI_HANDLE){indices[h]},(EFI_HANDLE){indices[a]},(EFI_HANDLE){indices[c]},{guid},{attrs},{count}}},')
    lines.append('};')
    for role in ('Rtx','OtherChild'):
        raw = path(blocks[roles[role]])
        if len(raw) != 36:
            raise ValueError('Unexpected display path')
        lines.append('static const UINT8 Fixture'+role+'Path[36]={'+','.join(str(x) for x in raw)+'};')
    for role in ('Out','Gfx','Nv','Usb'):
        block = blocks[roles[role+'Image']]
        first = re.search(r'image-file-path-node raw=([0-9a-f]+)\n((?:path-node raw=[0-9a-f]+\n)+)', block)
        raw = bytes.fromhex(first[1]) + path(first[2])
        lines.append('static const UINT8 Fixture'+role+'FilePath[]={'+','.join(str(x) for x in raw)+'};')
        image = one([im for im in evidence['images'] if int(im['handle'],16)==int(roles[role+'Image'],16)])
        lines.append(f'#define FX_{role}File "IMG{image["index"]:03d}.BIN"')
    lines.append('static const EFI_HANDLE FixtureUsbLeaves[]={FX_UsbLeaf0,FX_UsbLeaf1,FX_UsbLeaf2};')
    lines.append('static const UINT8 FixtureUsbOtherPath[]={'+','.join(str(x) for x in path(blocks[roles['UsbOtherChild']]))+'};')
    lines.append('static const UINT8 FixtureUsbPaths[3][40]={'+','.join('{'+','.join(str(x) for x in p)+'}' for p in usb_paths)+'};')
    usb_cfg = re.search(r'^pci config-dwords=([0-9a-f ]+)$',blocks[roles['UsbHost']],re.M)[1]
    lines.append('static const UINT32 FixtureUsbConfig[16]={'+','.join('0x'+x for x in usb_cfg.split())+'};')
    lines.append('static const UINT32 FixtureConfig[64]={'+','.join(hex(x) for x in evidence['pci_config'])+'};')
    offsets = [0,0x118128,0x118234,0x110100,0x1fa828,0x823804,0x82381c,0x823820,0x1183a4]
    lines.append('static const UINT32 FixtureCold[9]={'+','.join(hex(evidence['registers'][f'{x:08x}']) for x in offsets)+'};')
    output.mkdir(parents=True, exist_ok=False)
    with (output/'replay-fixture.h').open('x', encoding='ascii') as stream:
        stream.write('\n'.join(lines)+'\n')
    with (output/'fixture.json').open('x', encoding='utf-8') as stream:
        json.dump(dict(capture_sha256=evidence['capture_sha256'], handles=len(blocks), edges=len(edges), hardware_access=False), stream, indent=2)
    print(f'Verified real fixture: {len(blocks)} handles, {len(edges)} active edges')


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('capture', type=Path)
    p.add_argument('output', type=Path)
    a = p.parse_args()
    create(a.capture, a.output)
