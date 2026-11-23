"""Summarize round 1: prefetching headroom per trace under perfect branch prediction.

Usage: python scripts/round1_summary.py results/round1/runs.csv results/round1/summary.csv

headroom       = IPC(perfect data cache) / IPC(no prefetching) - 1, the most any prefetcher could add
stride_capture = share of that headroom the built-in stride prefetcher already gets
"""
import csv
import sys
from collections import defaultdict
from statistics import mean


def mpki(misses, instructions):
    return 1000 * int(misses) / int(instructions) if misses and instructions else 0.0


def main(runs_csv, out_csv):
    runs = defaultdict(dict)
    for r in csv.DictReader(open(runs_csv)):
        if r['ipc']:
            runs[(r['category'], r['trace'])][r['config']] = r

    rows = []
    for (category, trace), by_cfg in runs.items():
        if not {'nopf', 'stride', 'perfd'} <= by_cfg.keys():
            continue
        nopf, stride, perfd = by_cfg['nopf'], by_cfg['stride'], by_cfg['perfd']
        ipc_n, ipc_s, ipc_p = (float(x['ipc']) for x in (nopf, stride, perfd))
        gap = ipc_p - ipc_n
        rows.append({
            'category': category,
            'trace': trace,
            'instr_M': round(int(nopf['instructions']) / 1e6, 1),
            'ipc_nopf': ipc_n,
            'ipc_stride': ipc_s,
            'ipc_perfd': ipc_p,
            'headroom_pct': round(100 * (ipc_p / ipc_n - 1), 2),
            'stride_gain_pct': round(100 * (ipc_s / ipc_n - 1), 2),
            'stride_capture_pct': round(100 * (ipc_s - ipc_n) / gap, 1) if gap > 1e-9 else '',
            'l1_mpki_nopf': round(mpki(nopf['l1_miss'], nopf['instructions']), 2),
            'l2_mpki_nopf': round(mpki(nopf['l2_miss'], nopf['instructions']), 2),
            'l3_mpki_nopf': round(mpki(nopf['l3_miss'], nopf['instructions']), 2),
            'l1_mpki_stride': round(mpki(stride['l1_miss'], stride['instructions']), 2),
            'bp_mpki': round(mpki(nopf['cond_misp'], nopf['instructions']), 2),
        })
    rows.sort(key=lambda r: -r['headroom_pct'])

    with open(out_csv, 'w', newline='') as f:
        writer = csv.DictWriter(f, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)

    print(f'{len(rows)} traces with all three configs\n')
    print('headroom by category (mean / max):')
    by_cat = defaultdict(list)
    for r in rows:
        by_cat[r['category']].append(r['headroom_pct'])
    for cat, hs in sorted(by_cat.items()):
        print(f'  {cat:9s} n={len(hs):3d}  mean {mean(hs):6.2f}%  max {max(hs):6.2f}%')
    for t in (5, 10, 20):
        print(f'traces with headroom >= {t}%: {sum(r["headroom_pct"] >= t for r in rows)}')

    print('\ntop 30 by headroom:')
    print(f'{"trace":22s} {"headroom":>9s} {"stride":>8s} {"capture":>8s} {"L1mpki":>7s} {"L3mpki":>7s} {"bpMPKI":>7s}')
    for r in rows[:30]:
        print(f'{r["trace"]:22s} {r["headroom_pct"]:8.1f}% {r["stride_gain_pct"]:7.1f}% '
              f'{str(r["stride_capture_pct"]):>7s}% {r["l1_mpki_nopf"]:7.2f} {r["l3_mpki_nopf"]:7.2f} {r["bp_mpki"]:7.2f}')


if __name__ == '__main__':
    main(*sys.argv[1:3])
