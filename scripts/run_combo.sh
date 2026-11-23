#!/usr/bin/env bash
# Combined pollution controls for B-Fetch: fewer filter probes, a stricter per-load filter and
# LRU insertion of prefetches, with oracle and TAGE-SC-L lookahead. Uses bin/cbp_ft.
#   docker run --rm -v 'E:\ECE752-BFetch:/proj' cbp-dev bash /proj/scripts/run_combo.sh [jobs]
set -euo pipefail
JOBS=${1:-16}
export OUT=/proj/results/combo
export BIN=/proj/bin/cbp_ft
export BASE="-K brtc=4096,mht=2048,width=4,probe=64,filterthr=9,filterinit=3"
cd /proj/traces

run_one() {
    local var=$1 pred=$2 trace=$3 extra
    case $var in
        c0) extra="" ;;
        c1) extra=",llcpflru=1" ;;
        c2) extra=",llcpflru=1,l1pflru=1" ;;
    esac
    local log=$OUT/${var}_${pred}/$(basename "${trace%.gz}").log
    mkdir -p "$(dirname "$log")"
    if [ -s "$log" ] && grep -q '^IPC' "$log"; then return 0; fi
    if [ -e "$log.tmp" ]; then return 0; fi
    if $BIN -b -N -B $pred $BASE$extra "$trace" > "$log.tmp" 2>&1; then mv "$log.tmp" "$log"; else echo "FAILED $var $pred $trace"; fi
}
export -f run_one

for t in fp/fp_5 web/web_14 infra/infra_2 int/int_32 compress/compress_3 int/int_30 int/int_16 fp/fp_8 infra/infra_1 infra/infra_8 web/web_21; do
    echo "c0 core ${t}_trace.gz"   # c0 with oracle lookahead is t9p64i3 in results/filter
    for pred in oracle core; do
        for var in c1 c2; do echo "$var $pred ${t}_trace.gz"; done
    done
done | { cat; echo "c0 oracle int/int_30_trace.gz"; } | xargs -P "$JOBS" -L 1 bash -c 'run_one "$0" "$1" "$2"'
echo "combo done: $(find "$OUT" -name '*.log' | wc -l) logs"
