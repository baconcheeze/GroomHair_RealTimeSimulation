"""Score existing bounded Jolt app captures; no solver or groom generation."""
import csv
import hashlib
import json
import math
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'artifacts/run-styling'

def states(name):
    return {int(p.stem.split('-')[1]): json.loads(p.read_text())
            for p in sorted((OUT / name).glob('step-*.json'))}

def rms(a, b, ids):
    return math.sqrt(sum(math.dist(a[i], b[i]) ** 2 for i in ids) / len(ids))

runs = {n: states(n) for n in ('baseline', 'held-fringe', 'balanced', 'idle', 'Debug')}
assert all(runs.values())
summary = {'metric': 'Head-local guide-tip RMS distance from each run initial state, metres; includes gravity/shape offset, not pure oscillation amplitude.', 'runs': {}}
curve = []
for name, ss in runs.items():
    initial = ss[0]
    ids = initial['guideMaterials']
    groups = {'bangs': [i for i, m in enumerate(ids) if m == 1],
              'sideBack': [i for i, m in enumerate(ids) if m == 0]}
    assert all(groups.values())
    for step, s in ss.items():
        assert s['finite'] and s['maxRootAttachmentErrorMeters'] < 1e-5
        assert s['guideMaterials'] == ids
        assert s['guides'] == 1387 and s['render'] == 27734
        assert s['dt'] == initial['dt']
        for group, indices in groups.items():
            curve.append({'run': name, 'step': step, 'region': group,
                          'tipRmsFromInitialMetres': rms(s['guideTips'], initial['guideTips'], indices)})
        if name in ('held-fringe', 'balanced'):
            b = runs['baseline'][step]
            assert s['dt'] == b['dt'] and s['animationFrame'] == b['animationFrame']
            assert s['characterPositionWorld'] == b['characterPositionWorld']
        if name != 'baseline':
            assert s['materials'][0] != s['materials'][1]
    p = OUT / name
    assert (p / 'step-0000.png').read_bytes() == (p / 'reset-0000.png').read_bytes()
    summary['runs'][name] = {'steps': max(ss), 'snapshots': len(ss),
        'maxRootAttachmentErrorMetres': max(s['maxRootAttachmentErrorMeters'] for s in ss.values()),
        'resetPngExact': True, 'materials': initial['materials'],
        'guideRegionCounts': {k: len(v) for k, v in groups.items()}}
for name in ('character.bin', 'bob.hair', 'bob.materials', 'naruto-run.bin'):
    p = ROOT / 'assets/moca' / name
    summary.setdefault('assetSha256', {})[name] = hashlib.sha256(p.read_bytes()).hexdigest()
assert 'D3D12 debug audit: no errors' in (OUT / 'debug.log').read_text()
summary['debugLayerErrors'] = 0
with (OUT / 'tip-curves.csv').open('w', newline='') as f:
    writer = csv.DictWriter(f, fieldnames=list(curve[0]))
    writer.writeheader()
    writer.writerows(curve)
(OUT / 'validation.json').write_text(json.dumps(summary, indent=2))
print('PASS: finite states, pins, matched actor motion, region persistence, reset and debug audit')
