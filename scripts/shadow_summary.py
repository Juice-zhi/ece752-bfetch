"""Compare TAGE-SC-L and RUNLTS along the oracle's lookahead path, by depth (scripts/run_shadow.sh).

Both predictors see exactly the same branches, so the difference in mispredictions at each depth
shows how much of RUNLTS's advantage survives when RBias only has register values from the start
of the lookahead. Fetch-time numbers come from round 1 (TAGE-SC-L) and main_v1/runlts_nopf.
Usage: python scripts/shadow_summary.py results/shadow results/round1/runs.csv results/main_v1/runs.csv [base new]
base and new name the two folders under results/shadow to compare, tage and runlts by default.
"""
import csv
import re
import sys
from pathlib import Path

root = Path(sys.argv[1])
pat = re.compile(r'^Shadow core predictor at depth (\d+)-(\d+) : (\d+) checked, (\d+) correct', re.M)
data = {}
BASE, NEW = (sys.argv[4], sys.argv[5]) if len(sys.argv) > 5 else ('tage', 'runlts')
for cfg in (BASE, NEW):
    for log in (root / cfg).rglob('*.log'):
        rows = pat.findall(log.read_text(errors='replace'))
        if rows:
            data[(cfg, log.stem)] = {f'{a}-{b}': (int(n), int(n) - int(c)) for a, b, n, c in rows}
traces = sorted(t for c, t in data if c == BASE and (NEW, t) in data)
buckets = list(data[(BASE, traces[0])]) if traces else []
fetch = {}
for path, cfg, key in ((sys.argv[2], 'nopf', 'tage'), (sys.argv[3], 'runlts_nopf', 'runlts')):
    for r in csv.DictReader(open(path)):
        if r['config'] == cfg and r['cond_misp']:
            fetch[(key, r['trace'])] = int(r['cond_misp'])
print(f'{len(traces)} traces with both predictors')
print(f"{'depth':>8s} {'branches':>12s} {BASE + ' miss%':>15s} {NEW + ' miss%':>15s} {NEW + ' fewer':>15s}")
for b in buckets:
    n = sum(data[(BASE, t)][b][0] for t in traces)
    mt = sum(data[(BASE, t)][b][1] for t in traces)
    mr = sum(data[(NEW, t)][b][1] for t in traces)
    if n:
        print(f'{b:>8s} {n:12d} {100 * mt / n:14.3f}% {100 * mr / n:14.3f}% {100 * (1 - mr / mt) if mt else 0:14.1f}%')
ft = sum(fetch.get(('tage', t), 0) for t in traces)
fr = sum(fetch.get(('runlts', t), 0) for t in traces)
if ft and (BASE, NEW) == ('tage', 'runlts'):
    print(f'fetch time, same traces: RUNLTS makes {100 * (1 - fr / ft):.1f}% fewer conditional mispredictions than TAGE-SC-L')
