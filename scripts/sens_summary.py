"""Summarize the sensitivity runs of scripts/run_sens.sh next to the main_v5 baseline.

Usage: python scripts/sens_summary.py results/sens results/round1/runs.csv results/main_v5/runs.csv

For each config it prints, over the traces that config has finished:
  speedup  geometric mean IPC gain over no prefetching. Configs with -W are compared with
           no prefetching at the same -W, all others with round 1's no prefetching.
  focus    the same, over the 5 traces where the oracle gains at least 5% in v5
  depth    average lookahead depth in branches
  la_acc   share of lookahead conditional predictions that were right, while on the true path
  stop_cf  share of lookaheads stopped by the confidence threshold
  pf_acc   L1D prefetch accuracy, useful / (useful + useless)
The three shares are summed over all traces first, so long traces count more.
The v5 rows are main_v5 limited to the same traces as the sweep, so they are the 0.75 threshold,
brtc=4096 / mht=2048 and unlimited bandwidth points of every sweep.
"""
import csv
import math
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from parse_logs import parse  # noqa: E402

FOCUS = ['fp_1_trace', 'infra_1_trace', 'infra_5_trace', 'infra_7_trace', 'int_21_trace']
PRED_NAMES = {'bf_oracle': 'oracle', 'bf_tourn': 'tourn', 'bf_tage': 'tage', 'bf_runlts': 'runlts',
              'nopf': 'nopf', 'stride': 'stride'}


def number(row, key):
    value = row.get(key, '')
    if value == '':
        return 0.0
    return float(value)


def gmean_gain(rows, base_ipc):
    total = 0.0
    count = 0
    for trace, row in rows.items():
        if trace in base_ipc and number(row, 'ipc') > 0:
            total = total + math.log(number(row, 'ipc') / base_ipc[trace])
            count = count + 1
    if count == 0:
        return None
    return math.exp(total / count) - 1


def ratio(rows, top_key, bottom_keys):
    top = 0.0
    bottom = 0.0
    for row in rows.values():
        top = top + number(row, top_key)
        for key in bottom_keys:
            bottom = bottom + number(row, key)
    if bottom == 0:
        return None
    return top / bottom


def average(rows, key):
    values = []
    for row in rows.values():
        if row.get(key, '') != '':
            values.append(float(row[key]))
    if not values:
        return None
    return sum(values) / len(values)


def fmt(value, percent):
    if value is None:
        return '-'
    if percent:
        return f'{100 * value:+.1f}%' if percent == 'signed' else f'{100 * value:.1f}%'
    return f'{value:.1f}'


def main(sens_dir, round1_csv, v5_csv):
    # configs[name][trace] = row
    configs = {}
    for log in sorted(Path(sens_dir).glob('*/*/*.log')):
        row = parse(log.read_text(errors='replace'))
        if not row['ipc']:
            continue
        name = log.parts[-3]
        if name not in configs:
            configs[name] = {}
        configs[name][log.stem] = row
    if not configs:
        sys.exit(f'no finished logs under {sens_dir}')

    sweep_traces = set()
    for rows in configs.values():
        for trace in rows:
            sweep_traces.add(trace)

    nopf = {}
    for row in csv.DictReader(open(round1_csv)):
        if row['config'] == 'nopf' and row['ipc']:
            nopf[row['trace']] = float(row['ipc'])

    # main_v5 rows become configs named v5_<pred>, limited to the traces the sweep has run
    for row in csv.DictReader(open(v5_csv)):
        if row['config'] in PRED_NAMES and row['ipc'] and row['trace'] in sweep_traces:
            name = 'v5_' + PRED_NAMES[row['config']]
            if name not in configs:
                configs[name] = {}
            configs[name][row['trace']] = row

    print(f'{"config":16s} {"traces":>6s} {"speedup":>8s} {"focus":>8s} {"depth":>6s} {"la_acc":>7s} '
          f'{"stop_cf":>7s} {"pf_acc":>7s}')
    for name in sorted(configs):
        rows = configs[name]
        base = nopf
        if name.startswith('W'):
            setting = name.split('_')[0]
            base = {}
            for trace, row in configs.get(setting + '_nopf', {}).items():
                base[trace] = float(row['ipc'])
        focus_rows = {}
        for trace in FOCUS:
            if trace in rows:
                focus_rows[trace] = rows[trace]
        print(f'{name:16s} {len(rows):6d} '
              f'{fmt(gmean_gain(rows, base), "signed"):>8s} '
              f'{fmt(gmean_gain(focus_rows, base), "signed"):>8s} '
              f'{fmt(average(rows, "bf_depth"), False):>6s} '
              f'{fmt(ratio(rows, "bf_la_correct", ["bf_la_cond"]), True):>7s} '
              f'{fmt(ratio(rows, "bf_stop_conf", ["bf_lookaheads"]), True):>7s} '
              f'{fmt(ratio(rows, "l1_pf_useful", ["l1_pf_useful", "l1_pf_useless"]), True):>7s}')


if __name__ == '__main__':
    main(*sys.argv[1:4])
