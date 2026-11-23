"""Summarize the main experiment: B-Fetch driven by each lookahead predictor, with perfect
branch prediction in the core, against no prefetching, stride and a perfect data cache.

Usage: python scripts/main_summary.py results/round1/runs.csv results/main_v1/runs.csv \
           results/main_v1/summary.csv [results/round1/candidates_headroom20.txt]

kept_<pred> = (IPC with <pred> - IPC without prefetching) / (IPC with the oracle - IPC without
prefetching): the share of the oracle-driven B-Fetch gain that a real lookahead predictor keeps.
It is left empty where the oracle gain is under MIN_ORACLE_GAIN, since the ratio is noise there.
"""
import csv
import math
import sys
from collections import defaultdict
from statistics import mean

PREDS = ['bf_tourn', 'bf_tage', 'bf_runlts', 'bf_oracle']
NAMES = {'bf_tourn': 'tournament', 'bf_tage': 'TAGE-SC-L', 'bf_runlts': 'RUNLTS', 'bf_oracle': 'oracle'}
MIN_ORACLE_GAIN = 0.05


def load(path):
    runs = defaultdict(dict)
    for r in csv.DictReader(open(path)):
        if r['ipc']:
            runs[(r['category'], r['trace'])][r['config']] = r
    return runs


def num(x):
    return float(x) if x not in ('', None) else float('nan')


def ratio(a, b):
    return round(a / b, 4) if b else ''


def gmean(xs):
    xs = [x for x in xs if x == x and x > 0]
    return math.exp(sum(map(math.log, xs)) / len(xs)) if xs else float('nan')


def build_rows(round1, main, candidates):
    rows = []
    for key in sorted(round1):
        cat, trace = key
        r1, mn = round1[key], main.get(key, {})
        if not {'nopf', 'stride', 'perfd'} <= r1.keys() or not set(PREDS) <= mn.keys():
            continue
        instr = num(r1['nopf']['instructions'])
        base = num(r1['nopf']['ipc'])
        row = {
            'category': cat,
            'trace': trace,
            'candidate': int(candidates is None or f'{cat}/{trace}.gz' in candidates),
            'ipc_nopf': base,
            'ipc_stride': num(r1['stride']['ipc']),
            'ipc_perfd': num(r1['perfd']['ipc']),
        }
        for p in PREDS:
            row[f'ipc_{p}'] = num(mn[p]['ipc'])
        oracle_gain = row['ipc_bf_oracle'] / base - 1
        row['oracle_gain'] = round(oracle_gain, 4)
        for p in PREDS[:3]:
            row[f'kept_{p}'] = round((row[f'ipc_{p}'] - base) / (row['ipc_bf_oracle'] - base), 3) if oracle_gain >= MIN_ORACLE_GAIN else ''
        for p in PREDS:
            r = mn[p]
            row[f'depth_{p}'] = num(r['bf_depth'])
            row[f'la_acc_{p}'] = ratio(num(r['bf_la_correct']), num(r['bf_la_cond']))
            row[f'useful_{p}'] = num(r['l1_pf_useful'])
            row[f'useless_{p}'] = num(r['l1_pf_useless'])
        # Predictor accuracy at fetch; the core itself is always perfect in these runs.
        row['mpki_tage'] = round(1000 * num(r1['nopf']['cond_misp']) / instr, 3)
        if 'runlts_nopf' in mn:
            row['mpki_runlts'] = round(1000 * num(mn['runlts_nopf']['cond_misp']) / instr, 3)
        t = mn['bf_tourn']
        row['mpki_tourn'] = round(1000 * (num(t['tourn_cond']) - num(t['tourn_correct'])) / instr, 3)
        o = mn['bf_oracle']
        row['addr_exact_oracle'] = ratio(num(o['bf_primary_exact']), num(o['bf_primary']))
        rows.append(row)
    return rows


def report(rows, label):
    print(f'\n== {label}: {len(rows)} traces')
    cols = ['stride'] + PREDS + ['perfd']
    print('geomean speedup over no prefetching: ' + ', '.join(
        f"{NAMES.get(c, c)} {100 * (gmean([r[f'ipc_{c}'] / r['ipc_nopf'] for r in rows]) - 1):+.1f}%" for c in cols))
    meaningful = [r for r in rows if r['oracle_gain'] >= MIN_ORACLE_GAIN]
    if meaningful:
        print(f'share of the oracle gain kept, mean over {len(meaningful)} traces where the oracle gains >= {100 * MIN_ORACLE_GAIN:.0f}%: ' + ', '.join(
            f"{NAMES[p]} {100 * mean(r[f'kept_{p}'] for r in meaningful):.0f}%" for p in PREDS[:3]))
    print('mean lookahead depth: ' + ', '.join(f"{NAMES[p]} {mean(r[f'depth_{p}'] for r in rows):.1f}" for p in PREDS))
    print('mean lookahead branch accuracy: ' + ', '.join(
        f"{NAMES[p]} {100 * mean(r[f'la_acc_{p}'] for r in rows if r[f'la_acc_{p}'] != ''):.2f}%" for p in PREDS[:3]))
    mp = [('tournament', 'mpki_tourn'), ('TAGE-SC-L', 'mpki_tage'), ('RUNLTS', 'mpki_runlts')]
    print('mean fetch-time MPKI: ' + ', '.join(f'{n} {mean(r[k] for r in rows if k in r):.2f}' for n, k in mp))


def main(round1_csv, main_csv, out_csv, cand_file=None):
    candidates = set(open(cand_file).read().split()) if cand_file else None
    rows = build_rows(load(round1_csv), load(main_csv), candidates)
    if not rows:
        sys.exit('no trace has all configurations yet')
    fields = list(dict.fromkeys(k for r in rows for k in r))
    with open(out_csv, 'w', newline='') as f:
        writer = csv.DictWriter(f, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)
    report(rows, 'all traces')
    if candidates is not None:
        report([r for r in rows if r['candidate']], 'candidate set (round 1 headroom >= 20%)')


if __name__ == '__main__':
    main(*sys.argv[1:5])
