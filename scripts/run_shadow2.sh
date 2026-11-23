#!/usr/bin/env bash
# Upper bound for projecting register values to RBias: RUNLTS (bin/cbp_runlts_v4) scored along the
# oracle path with register values from the lookahead start (shadow=1) and with the true values
# written along the path (shadow=2). Same binary for both, on the focus set and the side-test traces.
#   docker run --rm -v 'E:\ECE752-BFetch:/proj' cbp-dev bash /proj/scripts/run_shadow2.sh [jobs]
set -euo pipefail
JOBS=${1:-16}
export OUT=/proj/results/shadow
export BIN=/proj/bin/cbp_runlts_v4
export KNOBS="-K brtc=4096,mht=2048,width=4,probe=64"
cd /proj/traces

run_one() {
    local cfg=$1 trace=$2 mode
    case $cfg in
        runlts_v4)    mode=1 ;;
        runlts_fresh) mode=2 ;;
    esac
    local log=$OUT/$cfg/${trace%.gz}.log
    mkdir -p "$(dirname "$log")"
    if [ -s "$log" ] && grep -q '^IPC' "$log"; then return 0; fi
    if [ -e "$log.tmp" ]; then return 0; fi
    if $BIN -b -N -B oracle $KNOBS,shadow=$mode "$trace" > "$log.tmp" 2>&1; then mv "$log.tmp" "$log"; else echo "FAILED $cfg $trace"; fi
}
export -f run_one

for t in fp/fp_0 fp/fp_13 fp/fp_1 fp/fp_4 fp/fp_5 fp/fp_8 infra/infra_0 infra/infra_1 infra/infra_3 infra/infra_4 \
         infra/infra_5 infra/infra_7 infra/infra_8 int/int_15 int/int_21 int/int_22 web/web_16 web/web_21 \
         infra/infra_2 int/int_32 web/web_14 compress/compress_3 int/int_16 int/int_30 compress/compress_0 int/int_0; do
    for cfg in runlts_fresh runlts_v4; do echo "$cfg ${t}_trace.gz"; done
done | xargs -P "$JOBS" -L 1 bash -c 'run_one "$0" "$1"'
echo "shadow2 done: $(find "$OUT"/runlts_v4 "$OUT"/runlts_fresh -name '*.log' | wc -l) logs"
