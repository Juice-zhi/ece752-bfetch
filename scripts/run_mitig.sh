#!/usr/bin/env bash
# Side test of cache-pollution mitigations for B-Fetch on a few traces.
# Uses bin/cbp_mt, a frozen copy of the current cbp2025/cbp that has the l1pflru, llcpflru and
# filterinit knobs. Variants: base (paper insertion, filter init 2), LRU insertion of prefetches
# in L2 and L3, that plus filter init 1, and LRU insertion in all three levels.
#   docker run --rm -v 'E:\ECE752-BFetch:/proj' cbp-dev bash /proj/scripts/run_mitig.sh [jobs]
set -euo pipefail
JOBS=${1:-8}
export OUT=/proj/results/mitig
export BIN=/proj/bin/cbp_mt
export BASE="-K brtc=4096,mht=2048,width=4"
cd /proj/traces

run_one() {
    local var=$1 pred=$2 trace=$3 extra
    case $var in
        base)    extra="" ;;
        llc)     extra=",llcpflru=1" ;;
        llc_fi1) extra=",llcpflru=1,filterinit=1" ;;
        all)     extra=",llcpflru=1,l1pflru=1" ;;
    esac
    local log=$OUT/${var}_${pred}/$(basename "${trace%.gz}").log
    mkdir -p "$(dirname "$log")"
    if [ -s "$log" ] && grep -q '^IPC' "$log"; then return 0; fi
    if $BIN -b -N -B $pred $BASE$extra "$trace" > "$log.tmp" 2>&1; then mv "$log.tmp" "$log"; else echo "FAILED $var $pred $trace"; fi
}
export -f run_one

for t in fp/fp_5 web/web_14 infra/infra_2 int/int_30 infra/infra_8 web/web_21; do
    for pred in oracle core; do
        for var in base llc llc_fi1 all; do echo "$var $pred ${t}_trace.gz"; done
    done
done | xargs -P "$JOBS" -L 1 bash -c 'run_one "$0" "$1" "$2"'
echo "mitig done: $(find "$OUT" -name '*.log' | wc -l) logs"
