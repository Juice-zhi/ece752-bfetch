"""Summarize the pollution-mitigation side test (scripts/run_mitig.sh).

Logs are at results/mitig/<variant>_<pred>/<trace>.log. Gains are relative to no prefetching
from round 1, which uses the same core with perfect branch prediction.
Usage: python scripts/mitig_summary.py results/mitig results/round1/runs.csv
"""
import csv
import sys
from collections import defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from parse_logs import parse  # noqa: E402

root, round1 = Path(sys.argv[1]), sys.argv[2]
nopf = {r['trace']: float(r['ipc']) for r in csv.DictReader(open(round1)) if r['config'] == 'nopf' and r['ipc']}
VARS = ['base', 'llc', 'llc_fi1', 'all']
res = defaultdict(dict)
for log in sorted(root.glob('*/*.log')):
    var, pred = log.parent.name.rsplit('_', 1)
    row = parse(log.read_text(errors='replace'))
    if row['ipc']:
        res[(log.stem, pred)][var] = row

print(f"{'trace':16s} {'pred':6s} {'nopf':>6s} " + ' '.join(f'{v:>16s}' for v in VARS))
for (trace, pred), d in sorted(res.items()):
    base = nopf.get(trace)
    cells = []
    for v in VARS:
        if v in d and base:
            r = d[v]
            gain = 100 * (float(r['ipc']) / base - 1)
            useless = int(r['l1_pf_useless'] or 0)
            useful = int(r['l1_pf_useful'] or 0)
            cells.append(f'{gain:+6.1f}% u{useful // 1000:>4d}k/{useless // 1000:>4d}k')
        else:
            cells.append(f"{'-':>16s}")
    print(f'{trace:16s} {pred:6s} {base or 0:6.3f} ' + ' '.join(cells))
