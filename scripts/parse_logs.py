"""Parse CBP2025 simulator logs into one CSV row per run.

Logs are expected at <root>/<config>/<category>/<trace>.log, as written by the run scripts.
Usage: python scripts/parse_logs.py results/round1 results/round1/runs.csv

Under perfect branch prediction (-b) the core never stalls on a misprediction, but cond_misp
still counts the bundled predictor's own mispredictions, since the predictor keeps training.
"""
import csv
import re
import sys
from pathlib import Path

SCALARS = {
    'instructions': r'^instructions\s*=\s*(\d+)',
    'cycles': r'^cycles\s*=\s*(\d+)',
    'ipc': r'^IPC\s*=\s*([\d.]+)',
    'cyc_wp': r'^CycWP\s*=\s*(\d+)',
    'pf_generated': r'^Num Prefetches generated :(\d+)',
    'pf_issued': r'^Num Prefetches issued :(\d+)',
    # B-Fetch section (empty when B-Fetch is off)
    'bf_lookaheads': r'^Lookaheads started :(\d+)',
    'bf_depth': r'^Avg lookahead depth :([\d.]+)',
    'bf_onpath_depth': r'^Avg on-path depth :([\d.]+)',
    'bf_stop_conf': r'^Lookahead stop, confidence :(\d+)',
    'bf_stop_brtc': r'^Lookahead stop, BrTC miss :(\d+)',
    'bf_la_cond': r'^Lookahead cond predictions checked :(\d+)',
    'bf_la_correct': r'^Lookahead cond predictions correct :(\d+)',
    'bf_candidates': r'^Prefetch candidates :(\d+)',
    'bf_onpath_cand': r'^Candidates on true path :(\d+)',
    'bf_primary': r'^Primary candidates on true path whose load executes in that block :(\d+)',
    'bf_primary_exact': r'^\s*same block as that load.s real address :(\d+)',
    'bf_filtered': r'^Candidates filtered by per-load filter :(\d+)',
    'bf_issued': r'^Prefetches issued :(\d+)',
    'tourn_cond': r'^Tournament cond branches :(\d+)',
    'tourn_correct': r'^Tournament cond correct :(\d+)',
    'mem_queue_delay': r'^Memory requests: \d+, avg bandwidth queueing delay: ([\d.]+)',
}
CACHE_FIELDS = (('accesses', 'acc'), ('misses', 'miss'), ('pf accesses', 'pf_acc'), ('pf misses', 'pf_miss'),
                ('pf useful', 'pf_useful'), ('pf late', 'pf_late'), ('pf useless', 'pf_useless'), ('pf redundant', 'pf_redundant'))


def parse(text):
    row = {}
    for key, pattern in SCALARS.items():
        m = re.search(pattern, text, re.M)
        row[key] = m.group(1) if m else ''
    m = re.search(r'^CondDirect\s+(\d+)\s+(\d+)', text, re.M)
    row['cond_br'], row['cond_misp'] = m.groups() if m else ('', '')
    # Measurement sections look like "L1$:" followed by tab-indented "accesses   = N" lines.
    # The configuration lines near the top ("L1$: 128 KB, ...") do not end at the colon, so they never match.
    for level in ('L1', 'L2', 'L3'):
        section = re.search(rf'^{level}\$:\n((?:\t.*\n)+)', text, re.M)
        body = section.group(1) if section else ''
        for field, name in CACHE_FIELDS:
            m = re.search(rf'^\t{field}\s*=\s*(\d+)', body, re.M)
            row[f'{level.lower()}_{name}'] = m.group(1) if m else ''
    return row


def main(root, out_csv):
    rows = []
    for log in sorted(Path(root).glob('*/*/*.log')):
        config, category = log.parts[-3], log.parts[-2]
        row = {'config': config, 'category': category, 'trace': log.stem}
        row.update(parse(log.read_text(errors='replace')))
        rows.append(row)
    if not rows:
        sys.exit(f'no logs under {root}')
    with open(out_csv, 'w', newline='') as f:
        writer = csv.DictWriter(f, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)
    incomplete = [r for r in rows if not r['ipc']]
    print(f'{len(rows)} runs written to {out_csv}' + (f', {len(incomplete)} without IPC' if incomplete else ''))


if __name__ == '__main__':
    main(*sys.argv[1:3])
