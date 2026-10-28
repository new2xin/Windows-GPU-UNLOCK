"""Derive private host fixtures from the complete v4 diagnostic.

stock.txt is generated test input, not an original firmware log.
Every captured image and PROM is authenticated by analyze_port.
"""
import argparse
import json
import re
from pathlib import Path
from analyze_port import analyze


def create(capture, output):
    evidence = analyze(capture)
    text = (capture / 'CAPTURE.TXT').read_text(encoding='utf-8-sig')
    trace = (capture / 'BOOTTRACE.TXT').read_text(encoding='utf-8-sig')
    if 'observer-version=0x0000000000000004\n' not in trace:
        raise ValueError('A complete v4 diagnostic is required')
    events = re.findall(r'^port-plan event=(\S+) status=0x([0-9a-f]+)$', text, re.M)
    required = {'review-display-route', 'review-original-graph',
                'review-console-consumers-code', 'review-projected-child-consumers',
                'review-nvidia-code', 'capture-pci-config256', 'capture-bar0',
                'capture-prom', 'capture-cold', 'release-plan-read-only-result'}
    if any(int(status, 16) for _, status in events) or any(
            sum(name == event for name, _ in events) != 1 for event in required):
        raise ValueError('Missing, ambiguous or failed live read-only preflight')
    r = evidence['registers']
    def reg(offset):
        return r[f'{offset:08x}']
    if reg(0x824148) != 1 or reg(0x8241e0) != 3:
        raise ValueError('Unreviewed Booter or FWSEC fuse word')
    if evidence['prom_sha256'] != '81915bf4d2ee2ca8c6069857b97a702d8269dbbb3767e51ec985f4ac5cf4047a':
        raise ValueError('Unreviewed PROM')
    cold = (f'cold state: chip={reg(0):#x} plm={reg(0x823804):#x} '
            f'gfw={reg(0x118128):#x}/{reg(0x118234):#x}/{reg(0x110100):#x} '
            f'WPR2_HI={reg(0x1fa828):x} SS0={reg(0x82381c):#x} SS1={reg(0x823820):#x}')
    lines = [f'# DERIVED host fixture; capture SHA256 {evidence["capture_sha256"]}', cold,
             f'memory={reg(0x1183a4):#x}']
    cfg = evidence['pci_config']
    for i in range(0, 64, 4):
        lines.append(f'config[{i*4:02x}]='+' '.join(f'{x:08x}' for x in cfg[i:i+4]))
    header = [f'#define FX_CAPTURE_SHA256 "{evidence["capture_sha256"]}"',
              '#define FX_COLD_LINE '+json.dumps(cold)]
    for name, offset in [('SS0',0x82381c),('SS1',0x823820),('BOOTER_FUSE',0x824148),
                         ('FWSEC_FUSE',0x8241e0),('MEMORY',0x1183a4),
                         ('MMU_LOW',0x1fa82c),('MMU_HIGH',0x1fa830)]:
        header.append(f'#define FX_{name} {reg(offset):#x}U')
    output.mkdir(parents=True, exist_ok=False)
    (output/'stock.txt').write_text('\n'.join(lines)+'\n', encoding='ascii')
    (output/'stock-fixture.h').write_text('\n'.join(header)+'\n', encoding='ascii')
    (output/'stock-provenance.json').write_text(json.dumps(dict(
        capture_path=str(capture.resolve()), capture_sha256=evidence['capture_sha256'],
        prom_sha256=evidence['prom_sha256'], registers=r,
        all_live_preflight_events_passed=True, generated_fixture=True,
        hardware_unlock_tested=False), indent=2)+'\n', encoding='utf-8')
    print('Verified v4 capture, live read-only preflight and fuse values; host fixture created')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    create(args.capture, args.output)
