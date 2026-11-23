/*

Copyright (c) 2019, North Carolina State University
All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.

3. The names “North Carolina State University”, “NCSU” and any trade-name, personal name,
trademark, trade device, service mark, symbol, image, icon, or any abbreviation, contraction or
simulation thereof owned by North Carolina State University must not be used to endorse or promote products derived from this software without prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

*/

// Author: Eric Rotenberg (ericro@ncsu.edu)

#ifndef _PARAMETERS_H_
#define _PARAMETERS_H_

enum class VPTracks
{
    ALL  = 0,
    LoadsOnly,
    LoadsOnlyHitMiss,
    NumTracks
};

extern bool VP_ENABLE;
extern bool VP_PERFECT;
extern uint64_t VP_TRACK;
extern uint64_t WINDOW_SIZE;
extern uint64_t FETCH_WIDTH;
extern uint64_t FETCH_NUM_BRANCH;
extern bool FETCH_STOP_AT_INDIRECT;
extern bool FETCH_STOP_AT_TAKEN;
extern bool FETCH_MODEL_ICACHE;

extern bool PERFECT_BRANCH_PRED;
extern bool PERFECT_INDIRECT_PRED;
extern uint64_t PIPELINE_FILL_LATENCY;
extern uint64_t NUM_LDST_LANES;
extern uint64_t NUM_ALU_LANES;

extern bool PREFETCHER_ENABLE;
extern bool PERFECT_CACHE;
extern bool WRITE_ALLOCATE;

extern uint64_t MEM_CYCLES_PER_BLOCK;   // main memory bandwidth limit, 0 = unlimited
extern bool PF_FILL_LRU_L1;             // insert prefetched blocks at LRU in the L1D
extern bool PF_FILL_LRU_LLC;            // ... and in the L2 and L3

extern bool BFETCH_ENABLE;
extern uint64_t BFETCH_PRED;            // lookahead predictor: 0 oracle, 1 tournament, 2 core predictor
extern double BFETCH_CONF_THRESHOLD;
extern uint64_t BFETCH_MAX_DEPTH;
extern uint64_t BFETCH_BRTC_ENTRIES;
extern uint64_t BFETCH_MHT_ENTRIES;
extern uint64_t BFETCH_LA_WIDTH;        // branches the lookahead follows per cycle
extern bool BFETCH_FILTER;              // per-load prefetch filter on/off
extern uint64_t BFETCH_FILTER_INIT;     // initial value of each per-load filter counter
extern uint64_t BFETCH_FILTER_THRESHOLD;  // a load may prefetch while its three counters sum to at least this
extern uint64_t BFETCH_PROBE_PERIOD;    // one blocked candidate in this many still goes out, 0 for none
extern uint64_t BFETCH_SHADOW;          // oracle lookahead also scores the core predictor: 1 with register
                                        // values from the lookahead start, 2 with true values along the path

extern uint64_t IC_SIZE;
extern uint64_t IC_ASSOC;
extern uint64_t IC_BLOCKSIZE;

extern uint64_t L1_SIZE;
extern uint64_t L1_ASSOC;
extern uint64_t L1_BLOCKSIZE;
extern uint64_t L1_LATENCY;

extern uint64_t L2_SIZE;
extern uint64_t L2_ASSOC;
extern uint64_t L2_BLOCKSIZE;
extern uint64_t L2_LATENCY;

extern uint64_t L3_SIZE;
extern uint64_t L3_ASSOC;
extern uint64_t L3_BLOCKSIZE;
extern uint64_t L3_LATENCY;

extern uint64_t MAIN_MEMORY_LATENCY;

extern uint64_t DEFAULT_EXEC_LATENCY;
extern uint64_t FP_EXEC_LATENCY;
extern uint64_t SLOW_ALU_EXEC_LATENCY;

extern uint64_t LOG_LEVEL;
extern uint64_t LOG_START_CYCLE;
extern uint64_t LOG_END_CYCLE;

extern uint64_t DQ_LATENCY;
extern uint64_t MISP_REDUCTION_PERC;

extern uint64_t EPOCH_SIZE_INSTS;
extern bool PRINT_PER_EPOCH_STATS;
#endif
