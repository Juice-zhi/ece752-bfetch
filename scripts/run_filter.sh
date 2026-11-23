#!/usr/bin/env bash
# Side test of stricter per-load filter settings for B-Fetch with oracle lookahead.
# bin/cbp_ft is a frozen copy of cbp2025/cbp with the filterthr and probe knobs.
#   docker run --rm -v 'E:\ECE752-BFetch:/proj' cbp-dev bash /proj/scripts/run_filter.sh [jobs]
# Several copies can run at once: a run whose .tmp file exists is left to the copy that started it.
set -euo pipefail
JOBS=${1:-8}
export OUT=/proj/results/filter
export BIN=/proj/bin/cbp_ft
export BASE="-K brtc=4096,mht=2048,width=4"
cd /proj/traces

run_one() {
    local var=$1 trace=$2 extra
    case $var in
        p64)      extra=",probe=64" ;;
        t6p64)    extra=",filterthr=6,probe=64" ;;
        t9p64)    extra=",filterthr=9,probe=64" ;;
        t9p64i3)  extra=",filterthr=9,probe=64,filterinit=3" ;;
        t12p64i4) extra=",filterthr=12,probe=64,filterinit=4" ;;
        regress)  extra="" ;;
    esac
    local log=$OUT/$var/$(basename "${trace%.gz}").log
    mkdir -p "$(dirname "$log")"
    if [ -s "$log" ] && grep -q '^IPC' "$log"; then return 0; fi
    if [ -e "$log.tmp" ]; then return 0; fi
    if [ "$var" = regress ]; then cmd="$BIN"; else cmd="$BIN -b -N -B oracle $BASE$extra"; fi
    if $cmd "$trace" > "$log.tmp" 2>&1; then mv "$log.tmp" "$log"; else echo "FAILED $var $trace"; fi
}
export -f run_one

{
    echo "regress media/media_0_trace.gz"
    for t in infra/infra_2 int/int_32 fp/fp_5 web/web_14 compress/compress_3 int/int_16 fp/fp_8 infra/infra_1 infra/infra_8 web/web_21; do
        for var in t9p64i3 p64 t6p64 t12p64i4 t9p64; do echo "$var ${t}_trace.gz"; done
    done
} | xargs -P "$JOBS" -L 1 bash -c 'run_one "$0" "$1"'
echo "filter done: $(find "$OUT" -name '*.log' | wc -l) logs"
