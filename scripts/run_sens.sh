#!/usr/bin/env bash
# Sensitivity tests for B-Fetch: path confidence threshold, table sizes and main memory bandwidth.
# Uses the frozen v5 binaries, so every run is directly comparable with results/main_v5. All three
# settings already exist there: -C <threshold>, -K brtc=<n>,mht=<n> and -W <cycles per block>.
# Run inside the cbp-dev container with the project mounted at /proj:
#   docker run --rm -v "$PWD:/proj" cbp-dev bash /proj/scripts/run_sens.sh [jobs]
# On an Apple Silicon Mac, add --platform linux/amd64 to both docker build and docker run,
# since the frozen binaries are x86-64.
#
# Environment:
#   SETS    which sweeps to run, any of "conf tables bw" (default: all three)
#   TRACES  "focus" for the 45 traces in results/round1/candidates_headroom20.txt (default),
#           or "all" for all 105 traces
#
# What each sweep runs (the main_v5 settings are brtc=4096, mht=2048, width=4, -C 0.75, no -W):
#   conf    -C 0.5, 0.6, 0.9 with tournament, TAGE-SC-L and RUNLTS. The oracle is left out because
#           it is always right, so its path confidence never falls below any threshold.
#   tables  the paper's sizes (brtc=256, mht=128) and a middle point (brtc=1024, mht=512), all four
#           predictors.
#   bw      -W 10 and -W 20 (one 64-byte block every 10 or 20 cycles) for no prefetching, stride,
#           and B-Fetch with all four predictors. No prefetching and stride are rerun at each -W,
#           because the round 1 numbers have unlimited bandwidth.
#
# Logs go to results/sens/<config>/<category>/<trace>.log. Finished runs are skipped, so the script
# can be restarted. Several copies can share the work. Delete leftover .tmp logs after a crash.
set -euo pipefail

JOBS=${1:-8}
SETS=${SETS:-"conf tables bw"}
TRACES=${TRACES:-focus}
export BIN=/proj/bin
export OUT=/proj/results/sens
export BASE="brtc=4096,mht=2048,width=4"
cd /proj/traces

# Prints the simulator command for one config name, for example "C0.6_tage", "T256_oracle" or "W10_stride".
command_for() {
    local cfg=$1
    local setting=${cfg%_*}
    local pred=${cfg##*_}
    local knobs=$BASE
    local flags=""

    case $setting in
        C*) flags="-C ${setting#C}" ;;
        T256) knobs="brtc=256,mht=128,width=4" ;;
        T1024) knobs="brtc=1024,mht=512,width=4" ;;
        W*) flags="-W ${setting#W}" ;;
    esac

    case $pred in
        nopf)    echo "$BIN/cbp_v5 -b -N $flags" ;;
        stride)  echo "$BIN/cbp_v5 -b $flags" ;;
        oracle)  echo "$BIN/cbp_v5 -b -N -B oracle -K $knobs $flags" ;;
        tourn)   echo "$BIN/cbp_v5 -b -N -B tournament -K $knobs $flags" ;;
        tage)    echo "$BIN/cbp_v5 -b -N -B core -K $knobs $flags" ;;
        runlts)  echo "$BIN/cbp_runlts_v5 -b -N -B core -K $knobs $flags" ;;
    esac
}
export -f command_for

run_one() {
    local cfg=$1
    local trace=$2
    local cmd
    cmd=$(command_for "$cfg")
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

configs=""
for s in $SETS; do
    case $s in
        conf)
            for c in 0.5 0.6 0.9; do
                for p in tourn tage runlts; do configs="$configs C${c}_$p"; done
            done ;;
        tables)
            for t in T256 T1024; do
                for p in oracle tourn tage runlts; do configs="$configs ${t}_$p"; done
            done ;;
        bw)
            for w in 10 20; do
                for p in nopf stride oracle tourn tage runlts; do configs="$configs W${w}_$p"; done
            done ;;
        *) echo "unknown set $s, use conf, tables or bw"; exit 1 ;;
    esac
done

if [ "$TRACES" = all ]; then
    traces=$(find . -name '*.gz' -printf '%s %P\n' | sort -rn | cut -d' ' -f2)
else
    traces=$(cat /proj/results/round1/candidates_headroom20.txt)
fi

for t in $traces; do
    for cfg in $configs; do echo "$cfg $t"; done
done | xargs -P "$JOBS" -L 1 bash -c 'run_one "$0" "$1"'

echo "sens done: $(find "$OUT" -name '*.log' | wc -l) logs"
