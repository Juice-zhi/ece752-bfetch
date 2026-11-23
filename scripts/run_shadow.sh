#!/usr/bin/env bash
# Shadow scoring: B-Fetch with oracle lookahead (v2 settings), while TAGE-SC-L (cbp_v3) or RUNLTS
# (cbp_runlts_v3) predicts every branch on the true path read-only. Both predictors are scored on
# exactly the same branches, by lookahead depth.
#   docker run --rm -v 'E:\ECE752-BFetch:/proj' cbp-dev bash /proj/scripts/run_shadow.sh [jobs]
# Several copies can share the work: a run whose .tmp log exists is left to the copy that started it.
set -euo pipefail
JOBS=${1:-28}
export OUT=/proj/results/shadow
export KNOBS="-K brtc=4096,mht=2048,width=4,probe=64,shadow=1"
cd /proj/traces

run_one() {
    local cfg=$1 trace=$2 bin
    case $cfg in
        tage)   bin=/proj/bin/cbp_v3 ;;
        runlts) bin=/proj/bin/cbp_runlts_v3 ;;
    esac
    local log=$OUT/$cfg/${trace%.gz}.log
    mkdir -p "$(dirname "$log")"
    if [ -s "$log" ] && grep -q '^IPC' "$log"; then return 0; fi
    if [ -e "$log.tmp" ]; then return 0; fi
    if $bin -b -N -B oracle $KNOBS "$trace" > "$log.tmp" 2>&1; then mv "$log.tmp" "$log"; else echo "FAILED $cfg $trace"; fi
}
export -f run_one

traces=$(find . -name '*.gz' -printf '%s %P\n' | sort -rn | cut -d' ' -f2)
for t in $traces; do
    for cfg in runlts tage; do echo "$cfg $t"; done
done | xargs -P "$JOBS" -L 1 bash -c 'run_one "$0" "$1"'
echo "shadow done: $(find "$OUT" -name '*.log' | wc -l) logs"
