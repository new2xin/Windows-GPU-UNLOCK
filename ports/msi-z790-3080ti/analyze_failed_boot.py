"""Compare complete before/after graphs from a failed physical wrapper run.

This reports observations and sufficient rejection conditions, not an invented
first-failed predicate. Old wrappers log only a shared recheck failure label.
"""
import argparse
import hashlib
import json
import re
from pathlib import Path

MARKER = 'gpu-console-graph-v1 observational-only; no-detach no-unlock no-nvram-write\n'
USB_HC = '3e745226-9818-45b6-a2ac-d7cd0e8ba2bc'


def graph(text):
    if text.count('capture-complete=1\n') != 1:
        raise ValueError('Incomplete or ambiguous graph')
    text = text.split('capture-complete=1\n')[0]
    blocks = dict(re.findall(r'^handle id=(\S+)\n(.*?)(?=^handle id=|\Z)', text, re.M | re.S))
    edges = set()
    for handle, block in blocks.items():
        protocol = None
        for line in block.splitlines():
            if line.startswith('protocol guid='):
                protocol = line[14:]
            match = re.fullmatch(r'open agent=(\S+) controller=(\S+) attributes=(\S+) count=(\S+)', line)
            if match and int(match[3], 16) not in (1, 2, 4):
                if protocol is None:
                    raise ValueError('Open without protocol')
                edges.add((handle, protocol, *match.groups()))
    return blocks, edges


def analyze(log, output):
    raw = log.read_bytes()
    text = raw.decode('utf-8-sig').replace('\r\n', '\n')
    parts = text.split(MARKER)[1:]
    if len(parts) != 2:
        raise ValueError('Expected two complete wrapper graphs')
    (before, old), (after, new) = map(graph, parts)
    events = re.findall(r'^([a-z][a-z0-9-]+) status=0x([0-9a-f]+)$', text, re.M)
    statuses = dict((name, int(status, 16)) for name, status in events)
    if statuses.get('full-release-recheck-failed') != 0x8000000000000006:
        raise ValueError('This analysis requires the observed pre-NVIDIA EFI_NOT_READY failure')
    if 'detach-nvidia-return' in statuses or 'original-unlock-return' in statuses:
        raise ValueError('Later hardware phases present; review manually')
    removed, added = sorted(old-new), sorted(new-old)
    usb = [edge for edge in removed if edge[1] == USB_HC]
    controllers = {}
    for edge in usb:
        block = before[edge[0]]
        controllers[edge[0]] = dict(
            pci=re.findall(r'^pci (.+)$', block, re.M),
            path_nodes=re.findall(r'^path-node raw=(.+)$', block, re.M))
    failed_checks = [name for name, status in events if name.startswith('release-recheck-') and int(status,16)]
    paired = []
    for edge in usb:
        route = re.findall(r'^path-node raw=(.+)$', before[edge[3]], re.M)
        candidates = [e for e in added if e[0:3]==edge[0:3] and e[4:]==edge[4:] and
                      re.findall(r'^path-node raw=(.+)$', after[e[3]], re.M)==route]
        paired.append(dict(old_child=edge[3],path_nodes=route,
                           replacement_children=[e[3] for e in candidates],unique_same_route_replacement=len(candidates)==1))
    result = dict(
        log_path=str(log.resolve()), log_sha256=hashlib.sha256(raw).hexdigest(),
        before_handles=len(before), after_handles=len(after),
        before_active_edges=len(old), after_active_edges=len(new),
        removed_active_edges=removed, added_active_edges=added,
        removed_usb_host_child_edges=usb, affected_usb_controllers=controllers,
        events=[dict(event=name, status=f'0x{status}') for name, status in events],
        console_text_detach_verified=statuses.get('detach-textout-verified') == 0,
        console_graphics_detach_verified=statuses.get('detach-graphics-console-verified') == 0,
        stopped_before_nvidia_disconnect=True, unlock_core_not_started=True,
        unexpected_usb_change_sufficient_to_fail_original_global_graph_predicate=bool(usb),
        exact_first_failed_recheck_predicate=failed_checks[0] if len(failed_checks)==1 else 'not recorded by original wrapper',
        usb_child_route_pairs=paired,
        all_three_usb_routes_have_unique_replacement_handles=len(paired)==3 and all(p['unique_same_route_replacement'] for p in paired),
        usb_descriptors_were_not_collected_in_these_physical_logs=True,
        hardware_unlock_succeeded=False)
    output.mkdir(parents=True, exist_ok=False)
    (output/'failure-analysis.json').write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
    print(f'Before/after active edges: {len(old)}/{len(new)}; removed USB child opens: {len(usb)}; exact predicate: {result["exact_first_failed_recheck_predicate"]}; NVIDIA stop and unlock not reached')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('log', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    analyze(args.log, args.output)
