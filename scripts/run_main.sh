#!/usr/bin/env bash
# Main experiment: B-Fetch driven by each lookahead predictor, on every training trace, with
# perfect branch prediction in the core so that only prefetching changes between configs.
# The no-prefetch, stride and perfect-cache runs come from round 1 (scripts/run_round1.sh).
# Run inside the cbp-dev container with the project mounted at /proj:
#   docker run --rm -v 'E:\ECE752-BFetch:/proj' cbp-dev bash /proj/scripts/run_main.sh [jobs] [tag] [extra knobs]
# The binaries are bin/cbp_<tag> and bin/cbp_runlts_<tag>. Extra knobs are appended to -K, for example
# probe=64. Set CONFIGS in the environment to run a subset of the configurations.
# Finished runs are skipped, so the script can simply be restarted after an interruption. Several
# copies can share the work. Delete leftover .tmp logs before restarting after a crash.
set -euo pipefail

JOBS=${1:-28}
TAG=${2:-v1}
export BIN=/proj/bin
export TAG
export OUT=/proj/results/main_$TAG
EXTRA=${3:-}
export KNOBS="-K brtc=4096,mht=2048,width=4${EXTRA:+,$EXTRA}"
export ORACLE_EXTRA=${ORACLE_EXTRA:-}
CONFIGS=${CONFIGS:-"bf_runlts bf_tage bf_oracle bf_tourn runlts_nopf"}
cd /proj/traces

run_one() {
    local cfg=$1 trace=$2 cmd
    case $cfg in
        bf_oracle) cmd="$BIN/cbp_$TAG -b -N -B oracle $KNOBS${ORACLE_EXTRA:+,$ORACLE_EXTRA}" ;;   # ORACLE_EXTRA=shadow=1 also scores TAGE-SC-L
        bf_tourn)  cmd="$BIN/cbp_$TAG -b -N -B tournament $KNOBS" ;;
        bf_tage)   cmd="$BIN/cbp_$TAG -b -N -B core $KNOBS" ;;
        bf_runlts) cmd="$BIN/cbp_runlts_$TAG -b -N -B core $KNOBS" ;;
        runlts_nopf) cmd="$BIN/cbp_runlts_$TAG -b -N" ;;   # RUNLTS accuracy without prefetching
    esac
    local log=$OUT/$cfg/${trace%.gz}.log
    mkdir -p "$(dirname "$log")"
    if [ -s "$log" ] && grep -q '^IPC' "$log"; then return 0; fi
    if [ -e "$log.tmp" ]; then return 0; fi   # another copy of this script is running it
    if $cmd "$trace" > "$log.tmp" 2>&1; then
        mv "$log.tmp" "$log"
    else
        echo "FAILED $cfg $trace"
    fi
}
export -f run_one

traces=$(find . -name '*.gz' -printf '%s %P\n' | sort -rn | cut -d' ' -f2)
for t in $traces; do
    for cfg in $CONFIGS; do echo "$cfg $t"; done
done | xargs -P "$JOBS" -L 1 bash -c 'run_one "$0" "$1"'

echo "main $TAG done: $(find "$OUT" -name '*.log' | wc -l) logs"
