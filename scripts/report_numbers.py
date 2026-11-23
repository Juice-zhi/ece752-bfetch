"""Numbers for the status report from a main_summary.py output CSV.

Usage: python scripts/report_numbers.py results/main_v1/summary.csv [results/main_v1/runs.csv]
"""
import csv
import math
import sys
from statistics import mean, median

rows = list(csv.DictReader(open(sys.argv[1])))
P = ['bf_tourn', 'bf_tage', 'bf_runlts', 'bf_oracle']
N = {'bf_tourn': 'tournament', 'bf_tage': 'TAGE-SC-L', 'bf_runlts': 'RUNLTS', 'bf_oracle': 'oracle'}
f = lambda r, k: float(r[k]) if r.get(k) not in ('', None) else float('nan')


def gm(xs):
    return math.exp(sum(map(math.log, xs)) / len(xs))


print(f'{len(rows)} traces')
for thr in (0.03, 0.05, 0.10):
    foc = [r for r in rows if f(r, 'oracle_gain') >= thr]
    print(f'oracle gain >= {thr:.0%}: {len(foc)} traces: ' + ' '.join(r['trace'].replace('_trace', '') for r in foc))
foc = [r for r in rows if f(r, 'oracle_gain') >= 0.05]
for p in P:
    sp_all = gm([f(r, f'ipc_{p}') / f(r, 'ipc_nopf') for r in rows]) - 1
    sp_foc = gm([f(r, f'ipc_{p}') / f(r, 'ipc_nopf') for r in foc]) - 1 if foc else float('nan')
    slow = sum(f(r, f'ipc_{p}') < 0.99 * f(r, 'ipc_nopf') for r in rows)
    fast = sum(f(r, f'ipc_{p}') > 1.01 * f(r, 'ipc_nopf') for r in rows)
    line = f'{N[p]:10s} gmean all {100 * sp_all:+5.1f}%  focus {100 * sp_foc:+5.1f}%  slower>1% on {slow:3d}  faster>1% on {fast:3d}'
    if p != 'bf_oracle' and foc:
        k = [f(r, f'kept_{p}') for r in foc]
        line += f'  kept mean {100 * mean(k):3.0f}% median {100 * median(k):3.0f}%'
    line += f"  depth all {mean(f(r, f'depth_{p}') for r in rows):4.1f} focus {mean(f(r, f'depth_{p}') for r in foc) if foc else float('nan'):4.1f}"
    print(line)
# How much RBias helps at fetch and during lookahead, relative to TAGE-SC-L
fetch_t = mean(f(r, 'mpki_tage') for r in rows)
fetch_r = mean(f(r, 'mpki_runlts') for r in rows)
la_t = mean(1 - f(r, 'la_acc_bf_tage') for r in rows)
la_r = mean(1 - f(r, 'la_acc_bf_runlts') for r in rows)
la_o = mean(1 - f(r, 'la_acc_bf_tourn') for r in rows)
print(f'fetch MPKI: tournament {mean(f(r, "mpki_tourn") for r in rows):.2f} TAGE {fetch_t:.2f} RUNLTS {fetch_r:.2f} -> RUNLTS {100 * (1 - fetch_r / fetch_t):.1f}% fewer')
print(f'lookahead misprediction rate: tournament {100 * la_o:.2f}% TAGE {100 * la_t:.2f}% RUNLTS {100 * la_r:.2f}% -> RUNLTS {100 * (1 - la_r / la_t):.1f}% fewer')
print(f"mean exact-block address accuracy with oracle lookahead: {100 * mean(f(r, 'addr_exact_oracle') for r in rows):.0f}%")
acc = [f(r, 'useful_bf_oracle') / (f(r, 'useful_bf_oracle') + f(r, 'useless_bf_oracle')) for r in rows if f(r, 'useful_bf_oracle') + f(r, 'useless_bf_oracle') > 0]
print(f'L1 prefetch accuracy useful/(useful+useless), oracle: median {100 * median(acc):.0f}%')

# How much of B-Fetch's output the per-load filter stops, from the oracle runs
import csv as _csv
runs_path = sys.argv[2] if len(sys.argv) > 2 else None
if runs_path:
    fr = [r for r in _csv.DictReader(open(runs_path)) if r['config'] == 'bf_oracle' and r['bf_candidates']]
    share = [int(r['bf_filtered']) / int(r['bf_candidates']) for r in fr if int(r['bf_candidates'])]
    print(f'per-load filter stops {100 * median(share):.0f}% of oracle candidates on the median trace, {100 * mean(share):.0f}% on average')
