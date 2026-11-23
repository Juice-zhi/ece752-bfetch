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


#include <algorithm>
#include <deque>
#include <utility>
#include <vector>

// Main memory bandwidth: one last-level block transfer every `period` cycles. A request takes
// the first free transfer slot at or after it reaches memory. The simulator issues requests out
// of time order, so slots are reserved individually rather than with a single "busy until" time.
class mem_bandwidth_t {
    uint64_t period;
    uint64_t base_slot = 0;
    std::deque<uint8_t> busy;   // busy[i] is slot base_slot + i
public:
    uint64_t requests = 0;
    uint64_t queued_cycles = 0;

    explicit mem_bandwidth_t(uint64_t cycles_per_block) : period(cycles_per_block) {}

    uint64_t reserve(uint64_t cycle) {
        uint64_t i = std::max(cycle / period, base_slot) - base_slot;
        while (true) {
            if (i >= busy.size()) busy.resize(i + 1, 0);
            if (!busy[i]) break;
            i++;
        }
        busy[i] = 1;
        const uint64_t start = std::max(cycle, (base_slot + i) * period);
        requests++;
        queued_cycles += start - cycle;
        return start;
    }

    // No request will arrive before `cycle` any more.
    void advance(uint64_t cycle) {
        const uint64_t slot = cycle / period;
        while (base_slot < slot && !busy.empty()) { busy.pop_front(); base_slot++; }
        base_slot = std::max(base_slot, slot);
    }
};

struct block_t {
    bool valid;
    //bool dirty;   // TO DO
    uint64_t tag;
    uint64_t timestamp;
    uint64_t lru;
    bool pf;        // brought in by a prefetch and not yet touched by a demand access
    uint64_t pf_pc; // load the prefetch was made for (0 if unknown)
};

#define IsPow2(x)   (((x) & (x-1)) == 0)

#define TAG(addr)   ((addr) >> (num_index_bits + num_offset_bits))
#define INDEX(addr) (((addr) >> num_offset_bits) & index_mask)

class cache_t {
private:
    block_t **C;
    uint64_t num_index_bits;
    uint64_t num_offset_bits;
    uint64_t index_mask;
    uint64_t assoc;

    // latency to search this cache for requested block
    uint64_t latency;

    // pointer to next cache level if applicable
    cache_t *next_level;

    // main memory bandwidth limit, used by the last level only (nullptr: unlimited)
    mem_bandwidth_t *mem_bw = nullptr;

    // measurements
    uint64_t accesses;
    uint64_t pf_accesses;
    uint64_t misses;
    uint64_t pf_misses;

    // Insert blocks brought in by prefetches at the LRU position instead of MRU, so an unused
    // prefetch is the next victim in its set and displaces less useful data.
    bool pf_fill_lru = false;

    // Prefetch accounting, kept only where prefetches are targeted (the L1D).
    bool track_pf = false;
    uint64_t pf_useful = 0;     // prefetched block later touched by a demand access
    uint64_t pf_late = 0;       // ... but the demand arrived before the block did
    uint64_t pf_useless = 0;    // prefetched block evicted untouched
    uint64_t pf_redundant = 0;  // prefetch for a block that was already present
    uint64_t demand_lat_sum = 0;  // sum over demand accesses of (cycle the block is available - access cycle)
    std::vector<std::pair<uint64_t, bool>> pf_feedback;   // (load pc, useful) events for B-Fetch's filter

    void update_lru(uint64_t index, uint64_t mru_way);

public:
    cache_t(uint64_t size, uint64_t assoc, uint64_t blocksize, uint64_t latency, cache_t *next_level);
    ~cache_t();
    uint64_t access(uint64_t cycle, bool read, uint64_t addr, bool pf = false, uint64_t pf_pc = 0);
    bool is_hit(uint64_t cycle, uint64_t addr) const;
    void stats();
    void enable_pf_tracking() { track_pf = true; }
    void set_mem_bandwidth(mem_bandwidth_t *bw) { mem_bw = bw; }
    void set_pf_fill_lru(bool lru) { pf_fill_lru = lru; }
    std::vector<std::pair<uint64_t, bool>> take_pf_feedback() { std::vector<std::pair<uint64_t, bool>> f; f.swap(pf_feedback); return f; }
};
