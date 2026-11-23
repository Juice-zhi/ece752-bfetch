"""Compare B-Fetch variants trace by trace against no prefetching from round 1.

Usage: python scripts/variant_summary.py results/round1/runs.csv label=dir [label=dir ...]
Each dir holds <trace>.log files, at any depth. A trace is listed if any variant has it.
"""
import csv
import math
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from parse_logs import parse  # noqa: E402

nopf = {r['trace']: float(r['ipc']) for r in csv.DictReader(open(sys.argv[1])) if r['config'] == 'nopf' and r['ipc']}
variants = [a.split('=', 1) for a in sys.argv[2:]]
res = {}
for label, d in variants:
    for log in Path(d).rglob('*.log'):
        row = parse(log.read_text(errors='replace'))
        if row['ipc']:
            res[(log.stem, label)] = row
traces = sorted({t for t, _ in res})
w = 20
print(f"{'trace':16s} {'nopf':>6s} " + ' '.join(f'{l:>{w}s}' for l, _ in variants))
logs = {l: [] for l, _ in variants}
for t in traces:
    cells = []
    for l, _ in variants:
        r = res.get((t, l))
        if r and t in nopf:
            gain = float(r['ipc']) / nopf[t]
            logs[l].append((t, gain))
            u, x = int(r['l1_pf_useful'] or 0), int(r['l1_pf_useless'] or 0)
            cells.append(f'{100 * (gain - 1):+6.1f}% {u // 1000:>5d}k/{x // 1000:>5d}k')
        else:
            cells.append(f"{'-':>{w}s}")
    print(f'{t:16s} {nopf.get(t, 0):6.3f} ' + ' '.join(cells))
common = set.intersection(*(set(t for t, _ in v) for v in logs.values())) if all(logs.values()) else set()
if common:
    print(f"{'gmean, ' + str(len(common)) + ' common':23s} " + ' '.join(
        f"{100 * (math.exp(sum(math.log(g) for t, g in logs[l] if t in common) / len(common)) - 1):+{w - 14}.1f}%{'':13s}" for l, _ in variants))
print('cells: IPC gain over no prefetching, then useful/useless L1 prefetches')
