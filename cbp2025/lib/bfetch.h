#pragma once
// B-Fetch: branch prediction directed data prefetching (Kadjo et al., MICRO 2014),
// adapted to the CBP2025 trace-driven timing model. docs/bfetch_design.md maps each
// structure of the paper to this code and lists where we had to make choices.

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <vector>
#include "sim_common_structs.h"
#include "core_lookahead.h"

// A branch in the dynamic stream. The simulator keeps the branch being simulated and the
// branches after it, read ahead from the trace, so the oracle can follow the true path and
// every lookahead can be checked against it.
struct BranchRecord
{
    uint64_t pc = 0;
    uint64_t next_pc = 0;               // taken target, or pc + 4 when not taken
    bool taken = false;
    bool cond = false;
    bool indirect = false;
    std::vector<std::pair<uint64_t, uint64_t>> loads;   // (pc, address) of the loads between this branch and the next
    std::vector<std::pair<uint64_t, uint64_t>> writes;  // (register, value) written from this branch up to the next, shadow mode 2 only
};

enum class LookaheadPredictor : uint8_t { Oracle = 0, Tournament = 1, Core = 2 };

// Alpha 21264 style tournament predictor, about 6.1 KB, close to the 6.55 KB baseline of the paper.
class TournamentPredictor
{
    static constexpr int LHT_BITS = 10;     // 1K local histories
    static constexpr int LHIST_BITS = 11;   // of 11 bits, indexing 2K 3-bit counters
    static constexpr int GHIST_BITS = 13;   // 8K global and 8K choice 2-bit counters
    static constexpr uint64_t LMASK = (1u << LHIST_BITS) - 1;
    static constexpr uint64_t GMASK = (1u << GHIST_BITS) - 1;
    std::array<uint16_t, 1 << LHT_BITS> lht{};
    std::array<uint8_t, 1 << LHIST_BITS> local_pht;
    std::array<uint8_t, 1 << GHIST_BITS> global_pht;
    std::array<uint8_t, 1 << GHIST_BITS> choice;   // >= 2 selects the global prediction

    static uint8_t sat(uint8_t c, bool up, uint8_t max) { return up ? (c < max ? c + 1 : c) : (c > 0 ? c - 1 : c); }
    static size_t lht_index(uint64_t pc) { return (pc >> 2) & ((1u << LHT_BITS) - 1); }

public:
    TournamentPredictor() { local_pht.fill(3); global_pht.fill(1); choice.fill(2); }

    uint16_t local_history(uint64_t pc) const { return lht[lht_index(pc)]; }

    bool predict(uint64_t ghist, uint16_t lhist) const
    {
        const bool local = local_pht[lhist & LMASK] >= 4;
        const bool global = global_pht[ghist & GMASK] >= 2;
        return choice[ghist & GMASK] >= 2 ? global : local;
    }

    void update(uint64_t pc, uint64_t ghist, bool taken)
    {
        uint16_t &lh = lht[lht_index(pc)];
        uint8_t &lc = local_pht[lh & LMASK];
        uint8_t &gc = global_pht[ghist & GMASK];
        const bool local = lc >= 4, global = gc >= 2;
        if (local != global)
            choice[ghist & GMASK] = sat(choice[ghist & GMASK], global == taken, 3);
        lc = sat(lc, taken, 7);
        gc = sat(gc, taken, 3);
        lh = ((lh << 1) | taken) & LMASK;
    }
};

// Path confidence in the style of Malik et al. (PaCo), which the paper builds on: a JRS
// resetting counter per branch gives its confidence level, and the measured correctness rate
// of each level is used as the probability that the branch is predicted correctly.
class PathConfidence
{
    static constexpr int ENTRIES = 2048;
    std::array<uint8_t, ENTRIES> jrs{};   // 4-bit resetting counters
    std::array<uint32_t, 16> correct{};
    std::array<uint32_t, 16> total{};

    static size_t index(uint64_t pc, uint64_t ghist) { return ((pc >> 2) ^ ((ghist & 0xff) << 3)) % ENTRIES; }

public:
    double probability(uint64_t pc, uint64_t ghist) const
    {
        const uint8_t level = jrs[index(pc, ghist)];
        return (correct[level] + 1.0) / (total[level] + 2.0);
    }

    void update(uint64_t pc, uint64_t ghist, bool was_correct)
    {
        uint8_t &level = jrs[index(pc, ghist)];
        correct[level] += was_correct;
        if (++total[level] >= (1u << 16)) {   // halve the counts so the rates keep adapting
            correct[level] >>= 1;
            total[level] >>= 1;
        }
        level = was_correct ? std::min<uint8_t>(level + 1, 15) : 0;
    }
};

class BFetch
{
public:
    // Sizes from Table I of the paper unless noted. BrTC and MHT sizes are defaults that
    // configure() can change, for the storage sensitivity study.
    static constexpr int BRTC_ENTRIES = 256;
    static constexpr int MHT_ENTRIES = 128;
    static constexpr int REGS_PER_MHT_ENTRY = 3;
    static constexpr int FILTER_ENTRIES = 2048;     // per table, three tables of 3-bit counters
    static constexpr int FILTER_THRESHOLD = 3;      // Table II
    static constexpr int PF_QUEUE_ENTRIES = 100;
    static constexpr int BTB_ENTRIES = 4096;        // stands in for the core's BTB, which B-Fetch reads for targets
    static constexpr int NUM_REGS = 66;             // int r0-r31, fp/simd r32-r63, flags r64, zero r65
    static constexpr int ZERO_REG = 65;
    static constexpr int ARF_DEPTH = 32;            // recent writes kept per register (see read_arf)
    static constexpr int RECENT_PF_ENTRIES = 64;    // our addition: skip blocks prefetched very recently
    static constexpr uint64_t PF_AGE_LIMIT = 32;    // cycles a queued prefetch may wait for a load/store slot
    static constexpr int MAX_DEPTH_LIMIT = 64;

    enum Stop { StopMaxDepth, StopConfidence, StopBrTCMiss, StopNoTarget, StopOffPath, NumStops };

    struct Config
    {
        LookaheadPredictor predictor = LookaheadPredictor::Oracle;
        double conf_threshold = 0.75;
        int max_depth = 32;
        int brtc_entries = BRTC_ENTRIES;
        int mht_entries = MHT_ENTRIES;
        int lookahead_width = 1;      // branches the lookahead stage follows per cycle
        bool per_load_filter = true;
        int filter_init = 2;          // each of the three counters
        int filter_threshold = FILTER_THRESHOLD;   // a load is allowed while its three counters sum to at least this
        int probe_period = 0;         // 0 as in the paper; N lets one blocked candidate in N through
        int shadow = 0;               // oracle mode only: also score the core predictor along the true path,
                                      // 1 with register values from the lookahead start, 2 with true values
    };

    BFetch()
    {
        for (auto &t : filter) t.fill(2);   // sum 6, so a load is blocked after a net two useless prefetches
        brtc.resize(BRTC_ENTRIES);
        mht.resize(MHT_ENTRIES);
    }

    void configure(const Config &c, CoreLookahead *core_lookahead)
    {
        mode = c.predictor;
        conf_threshold = c.conf_threshold;
        depth_limit = std::min(c.max_depth, MAX_DEPTH_LIMIT);
        la_width = std::max(c.lookahead_width, 1);
        use_filter = c.per_load_filter;
        filter_init = c.filter_init;
        filter_threshold = c.filter_threshold;
        probe_period = c.probe_period;
        shadow = c.shadow != 0 && c.predictor == LookaheadPredictor::Oracle && core_lookahead != nullptr;
        shadow_fresh = shadow && c.shadow == 2;
        for (auto &t : filter) t.fill(static_cast<uint8_t>(std::min(c.filter_init, 7)));
        brtc.assign(c.brtc_entries, BrTCEntry{});
        mht.assign(c.mht_entries, MHTEntry{});
        core = core_lookahead;
    }

    // ---------------------------------------------------------------- core pipeline hooks

    // A value produced by an instruction: becomes visible in the ARF at `ready`, its execute cycle.
    void record_write(uint64_t reg, uint64_t seq, uint64_t value, uint64_t ready)
    {
        if (reg >= 32) return;   // only integer registers form load addresses
        arf_head[reg] = (arf_head[reg] + 1) % ARF_DEPTH;
        arf[reg][arf_head[reg]] = {seq, value, ready};
        arf_count[reg] = std::min(arf_count[reg] + 1, ARF_DEPTH);
    }

    // A branch reaches decode at `decode_cycle`. recs[0] is this branch, recs[1..] the true
    // branches after it. Starts a lookahead if the engine is idle. Call before the branch
    // predictors are updated with this branch.
    void on_branch_decode(const std::deque<BranchRecord> &recs, uint64_t decode_cycle)
    {
        // The paper reads RegVal into the MHT from the ARF as the branch goes by. Offsets learned
        // against that value absorb how far the ARF lags behind fetch, and the lookahead reads
        // the same lagging ARF, so the lag cancels out.
        DecodeSnapshot s;
        for (int r = 0; r < 32; r++) s.valid[r] = read_arf(r, decode_cycle, s.value[r]);
        decode_snaps.push_back(s);

        if (decode_cycle >= engine_free_cycle)
            run_lookahead(recs, decode_cycle);
        else
            ++stat_engine_busy;
    }

    // After the core's predictor has handled the branch: train the tournament predictor, the
    // confidence estimator and the target table, as the main pipeline would at fetch.
    void on_branch_fetch(const BranchRecord &b, bool core_correct)
    {
        if (b.cond) {
            const bool tp = tournament.predict(ghist, tournament.local_history(b.pc));
            ++stat_tournament_cond;
            stat_tournament_correct += (tp == b.taken);
            const bool correct = mode == LookaheadPredictor::Oracle ? true
                               : mode == LookaheadPredictor::Tournament ? (tp == b.taken)
                               : core_correct;
            confidence.update(b.pc, ghist, correct);
            tournament.update(b.pc, ghist, b.taken);
            ghist = (ghist << 1) | b.taken;
        }
        if (b.indirect) {
            uint64_t t;
            ++indirect_total;
            indirect_correct += btb_lookup(b.pc, t) && t == b.next_pc;
        }
        if (b.taken) btb_update(b.pc, b.next_pc);
    }

    // An instruction commits: learn BrTC links and MHT offsets in program order.
    void on_commit(uint64_t pc, const ExecuteInfo &ei)
    {
        const InstClass cls = ei.dec_info.insn_class;
        if (cls == InstClass::loadInstClass && bb_valid && ei.mem_va.has_value() && !ei.dec_info.src_reg_info.empty()) {
            const uint64_t reg = ei.dec_info.src_reg_info[0];   // base register
            if (reg == ZERO_REG) learn_load(pc, reg, ei.mem_va.value(), 0);
            else if (reg < 32 && bb_snap.valid[reg]) learn_load(pc, reg, ei.mem_va.value(), bb_snap.value[reg]);
        }
        if (is_br(cls)) {
            const uint64_t target = ei.next_pc;
            const uint64_t key = bb_key(pc, target);
            if (have_prev_branch) brtc_update(prev_key, pc, is_cond_br(cls), is_uncond_ind_br(cls));
            prev_key = key;
            have_prev_branch = true;
            bb_key_cur = key;
            bb_valid = !decode_snaps.empty();
            if (bb_valid) {   // branches commit in the order they decoded
                bb_snap = decode_snaps.front();
                decode_snaps.pop_front();
            }
        }
    }

    // Feedback from the L1D about a block B-Fetch prefetched for `load_pc`.
    void filter_feedback(uint64_t load_pc, bool useful)
    {
        for (int t = 0; t < 3; t++) {
            uint8_t &c = filter[t][filter_index(load_pc, t)];
            c = useful ? std::min<uint8_t>(c + 1, 7) : (c > 0 ? c - 1 : 0);
        }
    }

    // ---------------------------------------------------------------- prefetch queue, same interface as StridePrefetcher

    // A prefetch may take any free load/store slot up to PF_AGE_LIMIT cycles after it was made,
    // including cycles in which fetch was stalled; the drain loop drops it after that.
    uint64_t last_issue_cycle(const Prefetch &p) const { return p.cycle_generated + PF_AGE_LIMIT; }
    void drop(const Prefetch &) { ++stat_dropped_old; --stat_issued; }

    bool issue(Prefetch &p, uint64_t cycle)
    {
        if (!queue.empty() && queue.front().cycle_generated <= cycle) {
            p = queue.front();
            queue.pop_front();
            ++stat_issued;
            return true;
        }
        return false;
    }

    void put_back(const Prefetch &p)
    {
        ++stat_put_back;
        --stat_issued;
        queue.push_front(p);
    }

    uint64_t get_oldest_pf_cycle() const { return queue.empty() ? MAX_CYCLE : queue.front().cycle_generated; }

    void print_stats() const
    {
        const double la = std::max<uint64_t>(stat_lookaheads, 1);
        printf("BFETCH_LOOKAHEAD_PREDICTOR = %s\n", mode == LookaheadPredictor::Oracle ? "oracle" : mode == LookaheadPredictor::Tournament ? "tournament" : "core");
        printf("BFETCH_CONF_THRESHOLD = %.2f\n", conf_threshold);
        printf("BFETCH_MAX_DEPTH = %d\n", depth_limit);
        printf("BFETCH_BRTC_ENTRIES = %zu\n", brtc.size());
        printf("BFETCH_MHT_ENTRIES = %zu\n", mht.size());
        printf("BFETCH_LOOKAHEAD_WIDTH = %d\n", la_width);
        printf("BFETCH_PER_LOAD_FILTER = %d\n", use_filter ? 1 : 0);
        printf("BFETCH_FILTER_INIT = %d\n", filter_init);
        printf("BFETCH_FILTER_THRESHOLD = %d\n", filter_threshold);
        printf("BFETCH_PROBE_PERIOD = %d\n", probe_period);
        printf("Lookaheads started :%lu\n", stat_lookaheads);
        printf("Branches skipped while engine busy :%lu\n", stat_engine_busy);
        printf("Avg lookahead depth :%.3f\n", stat_depth_sum / la);
        printf("Avg on-path depth :%.3f\n", stat_onpath_depth_sum / la);
        static const char *stop_names[NumStops] = {"max depth", "confidence", "BrTC miss", "no target", "off true path (oracle)"};
        for (int s = 0; s < NumStops; s++) printf("Lookahead stop, %s :%lu\n", stop_names[s], stat_stop[s]);
        printf("Lookahead cond predictions checked :%lu\n", stat_la_cond);
        printf("Lookahead cond predictions correct :%lu\n", stat_la_cond_correct);
        {   // depth buckets 0, 1, 2, 3, 4-7, 8-15, 16-31, 32 and deeper
            static const int lo[] = {0, 1, 2, 3, 4, 8, 16, 32}, hi[] = {0, 1, 2, 3, 7, 15, 31, MAX_DEPTH_LIMIT};
            for (int b = 0; b < 8; b++) {
                uint64_t n = 0, c = 0;
                for (int d = lo[b]; d <= hi[b]; d++) { n += stat_la_cond_d[d]; c += stat_la_correct_d[d]; }
                printf("Lookahead cond at depth %d-%d : %lu checked, %lu correct\n", lo[b], hi[b], n, c);
            }
            if (shadow) {
                printf("Shadow register values : %s\n", shadow_fresh ? "true values along the path" : "from the lookahead start");
                for (int b = 0; b < 8; b++) {
                    uint64_t n = 0, c = 0;
                    for (int d = lo[b]; d <= hi[b]; d++) { n += stat_shadow_d[d]; c += stat_shadow_correct_d[d]; }
                    printf("Shadow core predictor at depth %d-%d : %lu checked, %lu correct\n", lo[b], hi[b], n, c);
                }
            }
        }
        printf("Blocks visited on true path :%lu\n", stat_onpath_blocks);
        printf("Blocks visited off true path :%lu\n", stat_offpath_blocks);
        printf("MHT misses :%lu\n", stat_mht_miss);
        printf("Register values not yet available :%lu\n", stat_arf_unavailable);
        printf("Prefetch candidates :%lu\n", stat_candidates);
        printf("Candidates on true path :%lu\n", stat_onpath_candidates);
        printf("Candidates on true path with correct address :%lu\n", stat_onpath_correct);
        printf("Primary candidates on true path whose load executes in that block :%lu\n", stat_onpath_primary);
        printf("  same block as that load's real address :%lu\n", stat_primary_exact);
        printf("  within 4 blocks of it :%lu\n", stat_primary_near);
        printf("Candidates off true path :%lu\n", stat_offpath_candidates);
        printf("Candidates filtered by per-load filter :%lu\n", stat_filtered);
        printf("Candidates let through as filter probes :%lu\n", stat_probes);
        printf("Candidates dropped, recently prefetched :%lu\n", stat_dup_recent);
        printf("Candidates dropped, already queued :%lu\n", stat_dup_queue);
        printf("Candidates dropped, queue full :%lu\n", stat_queue_full);
        printf("Prefetches queued :%lu\n", stat_queued);
        printf("Prefetches issued :%lu\n", stat_issued);
        printf("Prefetches dropped, too old :%lu\n", stat_dropped_old);
        printf("Prefetches delayed by LDST contention :%lu\n", stat_put_back);
        printf("Tournament cond branches :%lu\n", stat_tournament_cond);
        printf("Tournament cond correct :%lu\n", stat_tournament_correct);
    }

private:
    struct BrTCEntry { bool valid = false; uint64_t key = 0; uint64_t next_pc = 0; bool next_cond = false; bool next_indirect = false; };
    struct RegHistory {
        bool valid = false;
        uint8_t reg = 0;
        uint64_t load_pc = 0;     // first load seen using this register in the block (indexes the filter)
        int64_t offset = 0;       // address minus register value at the block's leading branch
        int64_t loop_delta = 0;   // address change between consecutive executions of that load
        uint64_t last_ea = 0;
        uint8_t pos_patt = 0;     // further blocks touched by other loads off this register, +1..+5
        uint8_t neg_patt = 0;     // and -1..-5
    };
    struct MHTEntry { bool valid = false; uint64_t key = 0; std::array<RegHistory, REGS_PER_MHT_ENTRY> regs{}; uint8_t victim = 0; };
    struct RegWrite { uint64_t seq = 0; uint64_t value = 0; uint64_t ready = MAX_CYCLE; };
    struct DecodeSnapshot { std::array<uint64_t, 32> value{}; std::array<bool, 32> valid{}; };

    LookaheadPredictor mode = LookaheadPredictor::Oracle;
    double conf_threshold = 0.75;
    int depth_limit = 32;
    int la_width = 1;
    bool use_filter = true;
    int filter_init = 2;
    int filter_threshold = FILTER_THRESHOLD;
    int probe_period = 0;
    CoreLookahead *core = nullptr;

    TournamentPredictor tournament;
    PathConfidence confidence;
    uint64_t ghist = 0;
    uint64_t indirect_total = 0, indirect_correct = 0;
    std::array<std::pair<uint64_t, uint64_t>, BTB_ENTRIES> btb{};

    std::vector<BrTCEntry> brtc;
    std::vector<MHTEntry> mht;
    std::array<std::array<uint8_t, FILTER_ENTRIES>, 3> filter;
    uint64_t probe_counter = 0;

    std::array<std::array<RegWrite, ARF_DEPTH>, 32> arf{};
    std::array<int, 32> arf_head{}, arf_count{};

    // Commit-order learning state: ARF contents at the decode of every branch still in flight,
    // and those of the branch that leads the block now committing.
    std::deque<DecodeSnapshot> decode_snaps;
    DecodeSnapshot bb_snap;
    uint64_t bb_key_cur = 0, prev_key = 0;
    bool bb_valid = false, have_prev_branch = false;

    uint64_t engine_free_cycle = 0;
    std::deque<Prefetch> queue;
    std::array<uint64_t, RECENT_PF_ENTRIES> recent{};
    int recent_next = 0;

    uint64_t stat_lookaheads = 0, stat_engine_busy = 0, stat_depth_sum = 0, stat_onpath_depth_sum = 0;
    std::array<uint64_t, NumStops> stat_stop{};
    uint64_t stat_la_cond = 0, stat_la_cond_correct = 0, stat_onpath_blocks = 0, stat_offpath_blocks = 0;
    // Conditional predictions checked and correct, by how many blocks the lookahead had already
    // visited when it made them (0 is the branch that started the lookahead).
    std::array<uint64_t, MAX_DEPTH_LIMIT + 1> stat_la_cond_d{}, stat_la_correct_d{};
    // Shadow mode: the core predictor's read-only predictions along the oracle's true path.
    bool shadow = false, shadow_fresh = false;
    std::array<uint64_t, MAX_DEPTH_LIMIT + 1> stat_shadow_d{}, stat_shadow_correct_d{};
    uint64_t stat_mht_miss = 0, stat_arf_unavailable = 0, stat_candidates = 0, stat_onpath_candidates = 0;
    uint64_t stat_onpath_correct = 0, stat_offpath_candidates = 0, stat_filtered = 0, stat_probes = 0;
    uint64_t stat_onpath_primary = 0, stat_primary_exact = 0, stat_primary_near = 0;
    uint64_t stat_dup_recent = 0, stat_dup_queue = 0, stat_queue_full = 0, stat_queued = 0, stat_issued = 0;
    uint64_t stat_dropped_old = 0, stat_put_back = 0, stat_tournament_cond = 0, stat_tournament_correct = 0;

    // A basic block is named by the branch that leads to it and that branch's outcome,
    // as in the paper's hash of branch PC, direction and target.
    static uint64_t bb_key(uint64_t pc, uint64_t target)
    {
        uint64_t h = pc * 0x9E3779B97F4A7C15ull ^ (target + 0x632BE59BD9B4E019ull) * 0xC2B2AE3D27D4EB4Full;
        return h ^ (h >> 29);
    }

    static size_t filter_index(uint64_t pc, int t)
    {
        static constexpr uint64_t mult[3] = {0x9E3779B97F4A7C15ull, 0xC2B2AE3D27D4EB4Full, 0x165667B19E3779F9ull};
        return ((pc >> 2) * mult[t]) >> (64 - 11);
    }

    bool btb_lookup(uint64_t pc, uint64_t &target) const
    {
        const auto &e = btb[(pc >> 2) % BTB_ENTRIES];
        if (e.first != pc) return false;
        target = e.second;
        return true;
    }

    void btb_update(uint64_t pc, uint64_t target) { btb[(pc >> 2) % BTB_ENTRIES] = {pc, target}; }

    double indirect_probability() const { return (indirect_correct + 1.0) / (indirect_total + 2.0); }

    void brtc_update(uint64_t key, uint64_t next_pc, bool next_cond, bool next_indirect)
    {
        BrTCEntry &e = brtc[key % brtc.size()];
        e.valid = true;
        e.key = key;
        e.next_pc = next_pc;
        e.next_cond = next_cond;
        e.next_indirect = next_indirect;
    }

    const BrTCEntry *brtc_lookup(uint64_t key) const
    {
        const BrTCEntry &e = brtc[key % brtc.size()];
        return (e.valid && e.key == key) ? &e : nullptr;
    }

    // Offset = EA - RegVal (equation 1 of the paper), with RegVal the register's value at the
    // branch that led into the current block.
    void learn_load(uint64_t load_pc, uint64_t reg, uint64_t ea, uint64_t reg_val)
    {
        MHTEntry &m = mht[bb_key_cur % mht.size()];
        if (!m.valid || m.key != bb_key_cur) {
            m = MHTEntry{};
            m.valid = true;
            m.key = bb_key_cur;
        }
        const int64_t offset = static_cast<int64_t>(ea - reg_val);
        for (auto &r : m.regs) {
            if (!r.valid || r.reg != reg) continue;
            if (r.load_pc == load_pc) {
                r.loop_delta = static_cast<int64_t>(ea - r.last_ea);
                r.last_ea = ea;
                r.offset = offset;
            } else {
                const int64_t d = static_cast<int64_t>(ea >> 6) - static_cast<int64_t>((reg_val + r.offset) >> 6);
                if (d >= 1 && d <= 5) r.pos_patt |= 1 << (d - 1);
                else if (d <= -1 && d >= -5) r.neg_patt |= 1 << (-d - 1);
            }
            return;
        }
        RegHistory *slot = nullptr;
        for (auto &r : m.regs)
            if (!r.valid) { slot = &r; break; }
        if (!slot) {
            slot = &m.regs[m.victim];
            m.victim = (m.victim + 1) % REGS_PER_MHT_ENTRY;
        }
        *slot = RegHistory{};
        slot->valid = true;
        slot->reg = static_cast<uint8_t>(reg);
        slot->load_pc = load_pc;
        slot->offset = offset;
        slot->last_ea = ea;
    }

    // The ARF value of `reg` at `cycle`: the youngest write that has executed by then, which is
    // the paper's rule of only accepting updates from younger instructions.
    bool read_arf(uint64_t reg, uint64_t cycle, uint64_t &value) const
    {
        if (reg == ZERO_REG) { value = 0; return true; }
        if (reg >= 32) return false;
        for (int i = 0; i < arf_count[reg]; i++) {
            const RegWrite &w = arf[reg][(arf_head[reg] - i + ARF_DEPTH) % ARF_DEPTH];
            if (w.ready <= cycle) { value = w.value; return true; }
        }
        return false;
    }

    bool filter_allows(uint64_t load_pc) const
    {
        int sum = 0;
        for (int t = 0; t < 3; t++) sum += filter[t][filter_index(load_pc, t)];
        return sum >= filter_threshold;
    }

    using TruthLoads = std::vector<std::pair<uint64_t, uint64_t>>;

    void candidate(uint64_t block, uint64_t load_pc, uint64_t cycle, const TruthLoads *truth)
    {
        ++stat_candidates;
        if (truth) {   // does any load of that block instance touch this 64B block?
            ++stat_onpath_candidates;
            stat_onpath_correct += std::any_of(truth->begin(), truth->end(), [block](const auto &l) { return (l.second & ~63ull) == block; });
        } else {
            ++stat_offpath_candidates;
        }
        if (use_filter && !filter_allows(load_pc)) {
            // The paper does not say how a blocked load comes back, so by default it stays blocked.
            // probe_period > 0 is an experiment, not part of the design: one blocked candidate in N
            // still goes out, so a blocked load can earn its way back. Runs v1 and v2 used 16 and 64.
            if (probe_period == 0 || (++probe_counter % probe_period) != 0) { ++stat_filtered; return; }
            ++stat_probes;
        }
        if (std::find(recent.begin(), recent.end(), block) != recent.end()) { ++stat_dup_recent; return; }
        if (std::any_of(queue.begin(), queue.end(), [block](const Prefetch &q) { return q.address == block; })) { ++stat_dup_queue; return; }
        if (queue.size() >= PF_QUEUE_ENTRIES) { ++stat_queue_full; return; }
        Prefetch p{block, cycle};
        p.pc = load_pc;
        // Lookaheads produce non-decreasing cycles, but keep the queue sorted regardless.
        auto pos = std::upper_bound(queue.begin(), queue.end(), cycle, [](uint64_t c, const Prefetch &q) { return c < q.cycle_generated; });
        queue.insert(pos, p);
        recent[recent_next] = block;
        recent_next = (recent_next + 1) % RECENT_PF_ENTRIES;
        ++stat_queued;
    }

    // Register Lookup and Prefetch Calculate stages for one predicted block (equation 3).
    void generate(uint64_t key, int loop_cnt, uint64_t lookup_cycle, uint64_t gen_cycle, const TruthLoads *truth)
    {
        const MHTEntry &m = mht[key % mht.size()];
        if (!m.valid || m.key != key) { ++stat_mht_miss; return; }
        for (const auto &r : m.regs) {
            if (!r.valid) continue;
            uint64_t base;
            if (!read_arf(r.reg, lookup_cycle, base)) { ++stat_arf_unavailable; continue; }
            const uint64_t addr = base + r.offset + static_cast<int64_t>(loop_cnt) * r.loop_delta;
            const uint64_t block = addr & ~63ull;
            if (truth) {   // compare with the real address of the same load in that block instance
                const auto it = std::find_if(truth->begin(), truth->end(), [&r](const auto &l) { return l.first == r.load_pc; });
                if (it != truth->end()) {
                    ++stat_onpath_primary;
                    const int64_t dist = static_cast<int64_t>(block >> 6) - static_cast<int64_t>(it->second >> 6);
                    stat_primary_exact += dist == 0;
                    stat_primary_near += dist >= -4 && dist <= 4;
                }
            }
            candidate(block, r.load_pc, gen_cycle, truth);
            for (int d = 1; d <= 5; d++) {
                if ((r.pos_patt >> (d - 1)) & 1) candidate(block + 64 * d, r.load_pc, gen_cycle, truth);
                if ((r.neg_patt >> (d - 1)) & 1) candidate(block - 64 * d, r.load_pc, gen_cycle, truth);
            }
        }
    }

    // Branch Lookahead stage: one branch per cycle from the decoded branch, until the path
    // confidence falls below the threshold.
    void run_lookahead(const std::deque<BranchRecord> &recs, uint64_t start_cycle)
    {
        ++stat_lookaheads;
        if (mode == LookaheadPredictor::Core || shadow) core->begin();
        uint64_t la_ghist = ghist;
        std::array<std::pair<uint64_t, uint16_t>, MAX_DEPTH_LIMIT> local_spec;   // speculative local histories
        int n_local = 0;
        std::array<uint64_t, MAX_DEPTH_LIMIT> keys;
        int n_keys = 0;

        uint64_t pc = recs.front().pc;
        bool cond = recs.front().cond, indirect = recs.front().indirect;
        double conf = 1.0;
        bool on_path = true;
        int depth = 0, onpath_depth = 0;
        Stop reason = StopMaxDepth;

        while (depth < depth_limit) {
            const BranchRecord *truth = (on_path && static_cast<size_t>(depth) < recs.size()) ? &recs[depth] : nullptr;
            if (truth && truth->pc != pc) { on_path = false; truth = nullptr; }   // the BrTC led off the true path
            if (mode == LookaheadPredictor::Oracle && !truth) { reason = StopOffPath; break; }
            if (shadow_fresh && depth > 0)   // writes of the block the lookahead just crossed
                for (const auto &w : recs[depth - 1].writes) core->set_reg(w.first, w.second);

            bool dir = true;
            double p = 1.0;
            if (cond) {
                if (mode == LookaheadPredictor::Oracle) {
                    dir = truth->taken;
                    if (shadow) {
                        ++stat_shadow_d[depth];
                        stat_shadow_correct_d[depth] += (core->predict(pc) == truth->taken);
                    }
                } else if (mode == LookaheadPredictor::Tournament) {
                    uint16_t lh = tournament.local_history(pc);
                    for (int i = n_local - 1; i >= 0; i--)
                        if (local_spec[i].first == pc) { lh = local_spec[i].second; break; }
                    dir = tournament.predict(la_ghist, lh);
                    local_spec[n_local++] = {pc, static_cast<uint16_t>((lh << 1) | dir)};
                } else {
                    dir = core->predict(pc);
                }
                if (mode != LookaheadPredictor::Oracle) p = confidence.probability(pc, la_ghist);
                if (truth) {
                    ++stat_la_cond;
                    stat_la_cond_correct += (dir == truth->taken);
                    ++stat_la_cond_d[depth];
                    stat_la_correct_d[depth] += (dir == truth->taken);
                }
            }
            uint64_t target = pc + 4;
            if (dir) {
                if (mode == LookaheadPredictor::Oracle) target = truth->next_pc;   // the oracle knows indirect targets too
                else if (!btb_lookup(pc, target)) { reason = StopNoTarget; break; }
            }
            if (indirect && mode != LookaheadPredictor::Oracle) p *= indirect_probability();
            conf *= p;
            if (conf < conf_threshold) { reason = StopConfidence; break; }

            ++depth;
            if (truth && truth->next_pc != target) { on_path = false; truth = nullptr; }
            if (truth) { ++onpath_depth; ++stat_onpath_blocks; } else ++stat_offpath_blocks;

            const uint64_t key = bb_key(pc, target);
            int loop_cnt = 0;
            for (int i = 0; i < n_keys; i++) loop_cnt += keys[i] == key;
            keys[n_keys++] = key;
            // Lookahead finds this block in cycle start + (depth - 1) / width, Register Lookup
            // reads the ARF one cycle later, Prefetch Calculate produces addresses one cycle after that.
            const uint64_t found_cycle = start_cycle + (depth - 1) / la_width;
            generate(key, loop_cnt, found_cycle + 1, found_cycle + 2, truth ? &truth->loads : nullptr);

            const BrTCEntry *e = brtc_lookup(key);
            if (!e) { reason = StopBrTCMiss; break; }
            if (cond) la_ghist = (la_ghist << 1) | dir;
            if (mode == LookaheadPredictor::Core || shadow) core->push(pc, cond, indirect, dir, target);
            pc = e->next_pc;
            cond = e->next_cond;
            indirect = e->next_indirect;
        }
        ++stat_stop[reason];
        stat_depth_sum += depth;
        stat_onpath_depth_sum += onpath_depth;
        engine_free_cycle = start_cycle + std::max((depth + la_width - 1) / la_width, 1);
    }
};
