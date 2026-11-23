#!/usr/bin/env bash
# Round 1 trace characterization (step 1): every training trace with perfect branch
# prediction under three memory configurations, to find where prefetching has headroom.
#   nopf   : no prefetching                                   (-b -N)
#   stride : built-in L1D stride prefetcher                   (-b)
#   perfd  : perfect data cache, the bound for any prefetcher (-b -d -N)
# Run inside the cbp-dev container with the project mounted at /proj:
#   docker run --rm -v 'E:\ECE752-BFetch:/proj' cbp-dev bash /proj/scripts/run_round1.sh [jobs]
# Finished runs are skipped, so the script can simply be restarted after an interruption.
set -euo pipefail

JOBS=${1:-28}
export SIM=/proj/cbp2025/cbp
export OUT=/proj/results/round1
cd /proj/traces

run_one() {
    local cfg=$1 trace=$2 flags
    case $cfg in
        nopf)   flags="-b -N" ;;
        stride) flags="-b" ;;
        perfd)  flags="-b -d -N" ;;
    esac
    local log=$OUT/$cfg/${trace%.gz}.log
    mkdir -p "$(dirname "$log")"
    if [ -s "$log" ] && grep -q '^IPC' "$log"; then return 0; fi
    if $SIM $flags "$trace" > "$log.tmp" 2>&1; then
        mv "$log.tmp" "$log"
    else
        echo "FAILED $cfg $trace"
    fi
}
export -f run_one

# Longest traces first, so the parallel jobs finish close together.
traces=$(find . -name '*.gz' -printf '%s %P\n' | sort -rn | cut -d' ' -f2)
for t in $traces; do
    for cfg in nopf stride perfd; do echo "$cfg $t"; done
done | xargs -P "$JOBS" -L 1 bash -c 'run_one "$0" "$1"'

echo "round 1 done: $(find "$OUT" -name '*.log' | wc -l) logs"
