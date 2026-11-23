#!/usr/bin/env bash
# Shadow scoring for run v5, the paper's per-load filter with no probes. TAGE-SC-L is scored in
# results/main_v5/bf_oracle (run_main.sh with ORACLE_EXTRA=shadow=1). This script adds RUNLTS from
# the same build: register values from the lookahead start on every trace, and true values along
# the path on the 26 traces of run_shadow2.sh.
#   docker run --rm -v 'E:\ECE752-BFetch:/proj' cbp-dev bash /proj/scripts/run_shadow_v5.sh [jobs]
set -euo pipefail
JOBS=${1:-8}
export OUT=/proj/results/shadow_v5
export BIN=/proj/bin/cbp_runlts_v5
export KNOBS="-K brtc=4096,mht=2048,width=4,probe=0"
cd /proj/traces

run_one() {
    local cfg=$1 trace=$2 mode
    case $cfg in
        runlts)       mode=1 ;;
        runlts_fresh) mode=2 ;;
    esac
    local log=$OUT/$cfg/${trace%.gz}.log
    mkdir -p "$(dirname "$log")"
    if [ -s "$log" ] && grep -q '^IPC' "$log"; then return 0; fi
    if [ -e "$log.tmp" ]; then return 0; fi
    if $BIN -b -N -B oracle $KNOBS,shadow=$mode "$trace" > "$log.tmp" 2>&1; then mv "$log.tmp" "$log"; else echo "FAILED $cfg $trace"; fi
}
export -f run_one

{
    for t in fp/fp_0 fp/fp_13 fp/fp_1 fp/fp_4 fp/fp_5 fp/fp_8 infra/infra_0 infra/infra_1 infra/infra_3 infra/infra_4 \
             infra/infra_5 infra/infra_7 infra/infra_8 int/int_15 int/int_21 int/int_22 web/web_16 web/web_21 \
             infra/infra_2 int/int_32 web/web_14 compress/compress_3 int/int_16 int/int_30 compress/compress_0 int/int_0; do
        echo "runlts_fresh ${t}_trace.gz"
    done
    find . -name '*.gz' -printf '%s %P\n' | sort -rn | cut -d' ' -f2 | sed 's/^/runlts /'
} | xargs -P "$JOBS" -L 1 bash -c 'run_one "$0" "$1"'
echo "shadow_v5 done: $(find "$OUT" -name '*.log' | wc -l) logs"
