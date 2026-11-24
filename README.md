# ECE/CS 752 project: B-Fetch with modern branch predictors

## Layout

| Folder | Contents |
|---|---|
| `cbp2025/` | CBP2025 simulator from github.com/ramisheikh/cbp2025 at commit 6074966, with our changes listed in `docs/bfetch_design.md` |
| `docker/` | `Dockerfile` for the build and run environment, image tag `cbp-dev` |
| `traces/` | The 105 CBP2025 training traces in `compress/ fp/ infra/ int/ media/ web/`, not in git. Download them from the official CBP2025 training-trace Google Drive folder linked on the CBP2025 page and unpack each category into `traces/` |
| `runlts/` | RUNLTS release and paper from the CBP2025 workshop page, not in git. The source we use is in `cbp2025/predictors/runlts/` |
| `scripts/` | Run, parse and summary scripts |
| `results/` | Simulator logs and CSV summaries |
| `bin/` | Frozen simulator binaries, one set per results folder |
| `docs/` | `bfetch_design.md` maps the paper to our code, `status_report.docx` is built from `status_report.json` |

## Setup

The simulator needs Linux, g++, make and zlib. On Windows we build and run it in Docker.

```bash
docker build -t cbp-dev docker/
docker run --rm -v "$PWD:/proj" -w /proj/cbp2025 cbp-dev bash -c 'make clean && make -j8'
```

From Git Bash on Windows, set `MSYS_NO_PATHCONV=1` and pass a Windows path to `-v`, for example `-v 'E:\ECE752-BFetch:/proj'`.

`make` does not relink `cbp` when only files in `lib/` change, so use `make clean && make` or delete `cbp` first.

## Our changes to the simulator

1. `lib/cbp.cc`: new flag `-N` turns off the L1D stride prefetcher. It is on by default and the original code had no way to disable it. `-P` is now a no-op.
2. `lib/uarchsim.cc`: with `-b`, the original code skipped the branch predictor entirely, and the bundled TAGE-SC-L then crashed with `std::out_of_range` at update time. The predictor is now always called and trained, and under `-b` the core just ignores its mispredictions. The log still reports the predictor's own mispredictions, which gives its accuracy with a perfect core. Runs without `-b` give exactly the same results as before.

Useful flags: `-b` perfect branch prediction, `-N` no prefetching, `-d` perfect data cache, `-D` cache sizes and latencies.

## Round 1: prefetching headroom under perfect branch prediction

Following the proposal feedback, the focus set is chosen by prefetching headroom alone, with perfect branch prediction.

```bash
docker run --rm -v "$PWD:/proj" cbp-dev bash /proj/scripts/run_round1.sh 28
python scripts/parse_logs.py results/round1 results/round1/runs.csv
python scripts/round1_summary.py results/round1/runs.csv results/round1/summary.csv
```

Each trace runs three times: no prefetching (`-b -N`), stride prefetching (`-b`) and a perfect data cache (`-b -d -N`). `headroom` is how much faster the perfect data cache is than no prefetching, and `stride_capture` is the share of that headroom the stride prefetcher already gets. The full run takes about half an hour with 28 parallel jobs and can be restarted, since finished runs are skipped.

## Main experiment: B-Fetch with each lookahead predictor

The core always uses perfect branch prediction, so only the predictor that drives B-Fetch's lookahead changes between runs. Every trace runs B-Fetch with the oracle, the tournament predictor, TAGE-SC-L (`cbp -B core`) and RUNLTS (`cbp_runlts -B core`), all with `-K brtc=4096,mht=2048,width=4`, plus RUNLTS without prefetching to measure its accuracy. The binaries are frozen in `bin/` so that source changes do not mix into a running batch.

```bash
docker run --rm -v "$PWD:/proj" cbp-dev bash /proj/scripts/run_main.sh 28 v1
docker run --rm -v "$PWD:/proj" -e CONFIGS="bf_runlts bf_tage bf_oracle bf_tourn" cbp-dev bash /proj/scripts/run_main.sh 28 v2 probe=64
python scripts/parse_logs.py results/main_v1 results/main_v1/runs.csv
python scripts/main_summary.py results/round1/runs.csv results/main_v1/runs.csv results/main_v1/summary.csv results/round1/candidates_headroom20.txt
python scripts/report_numbers.py results/main_v1/summary.csv
```

`v5` is the main run: the paper's per-load filter with no probes (`probe=0`, the default), started with `ORACLE_EXTRA=shadow=1` so the oracle runs also score TAGE-SC-L, plus `scripts/run_shadow_v5.sh` for RUNLTS. `v1` and `v2` let one blocked filter candidate in 16 or 64 through as a probe, which is not in the paper, and are kept only as a diagnostic. All runs reuse the RUNLTS-only runs of `v1`. Several copies of `run_main.sh` can share a batch, since a run whose `.tmp` log exists is left to the copy that started it.

Results of `v5`, the main run, on all 105 traces: the paper's per-load filter stops 86% of B-Fetch's candidates on the median trace, so B-Fetch slows no trace by more than 1% but the oracle gains at least 5% on only 5 traces (fp_1, infra_1, infra_5, infra_7, int_21). On those, the tournament predictor keeps 38% of the oracle's gain on average, TAGE-SC-L 42% and RUNLTS 44%. Only 38% of B-Fetch's predicted addresses fall in the right block.

## Sensitivity: confidence threshold, table sizes and bandwidth

`scripts/run_sens.sh` reruns B-Fetch with the frozen v5 binaries, so the results compare directly with `main_v5`. No code change was needed: `-C`, `-K brtc=,mht=` and `-W` already exist in those binaries.

```bash
docker run --rm -v "$PWD:/proj" cbp-dev bash /proj/scripts/run_sens.sh 28
python scripts/sens_summary.py results/sens results/round1/runs.csv results/main_v5/runs.csv
```

| Sweep (`SETS`) | Values | Predictors |
|---|---|---|
| `conf` | `-C 0.5`, `0.6`, `0.9` (v5 uses 0.75) | tournament, TAGE-SC-L, RUNLTS; the oracle never loses confidence |
| `tables` | paper's `brtc=256,mht=128`, and `brtc=1024,mht=512` (v5 uses 4096 and 2048) | all four |
| `bw` | `-W 10` and `-W 20` cycles per 64-byte block (v5 is unlimited) | no prefetching, stride and all four |

By default it runs the 45 traces in `results/round1/candidates_headroom20.txt`, 1305 runs in total. `TRACES=all` runs all 105 traces, and `SETS="conf"` runs one sweep. The summary prints the v5 rows on the same traces as the reference point of each sweep.

On an Apple Silicon Mac, add `--platform linux/amd64` to `docker build` and `docker run`, because the frozen binaries are x86-64. They then run under emulation and take longer.

## Side tests on cache pollution

| Folder | Script | What varies |
|---|---|---|
| `results/mitig` | `scripts/run_mitig.sh` | LRU insertion of prefetches in L2 and L3, in all levels, filter start value |
| `results/filter` | `scripts/run_filter.sh` | probe period and filter threshold, oracle lookahead |
| `results/diag` | by hand with `bin/cbp_diag` | infra_2 with average demand latency per cache level |
| `results/shadow` | `scripts/run_shadow.sh`, `scripts/run_shadow2.sh` | TAGE-SC-L and RUNLTS scored on the same oracle-path branches by depth, and RUNLTS with true register values |

`python scripts/variant_summary.py results/round1/runs.csv v1=results/main_v1/bf_oracle p64=results/filter/p64` compares variants trace by trace.

Shadow results, v5: on the same branches, RUNLTS makes 14% fewer mispredictions than TAGE-SC-L on the first lookahead branch but only 5% fewer at depth 16 to 31, over all 105 traces. Giving RUNLTS the true register values along the path cuts its deep mispredictions by up to 6% on 26 traces and brings its edge back from 5% to 11%, so stale values explain most of the drop. TAGE-SC-L's shadow scores are in `results/main_v5/bf_oracle`, RUNLTS's in `results/shadow_v5`. `python scripts/shadow_summary.py results/shadow results/round1/runs.csv results/main_v1/runs.csv [base new]` prints the table.

## Third-party code

- `cbp2025/` comes from github.com/ramisheikh/cbp2025 at commit 6074966. Its files keep their original BSD-style notices from NC State University and ARM.
- `cbp2025/lib/spdlog/` is the spdlog logging library with the bundled fmt library, both under the MIT license.
- `cbp2025/predictors/runlts/` is RUNLTS by Koizumi et al., the winning entry of the 2025 Championship Branch Prediction, taken from the public release on the CBP2025 workshop page. We added the read-only lookahead methods described in `docs/bfetch_design.md`. The release came without a license file. All credit for the predictor goes to its authors, and we will take the code down if they ask.
