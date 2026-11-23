# B-Fetch in the CBP2025 simulator

This note maps the B-Fetch design (Kadjo et al., MICRO 2014) onto our implementation in `cbp2025/lib/bfetch.h`, and lists every place where we had to make a choice the paper does not settle. It also lists all changes we made to the CBP2025 simulator itself.

## How B-Fetch works, in one paragraph

When the core decodes a branch, B-Fetch walks ahead along the predicted path, one branch at a time, using the branch predictor for directions and a Branch Trace Cache (BrTC) to jump from a branch to the branch that ends the next basic block. For every block on that path it looks up the Memory History Table (MHT), which says which registers the block's loads use as base registers and at what offset. It adds that offset to the register's current value from the Alternate Register File (ARF) and prefetches the result. A path confidence estimator stops the walk once the predicted path is likely wrong, and a per-load filter stops prefetching for loads whose prefetches have not been useful.

## Paper structure to code

| Paper | Our code | Notes |
|---|---|---|
| Decoded Branch Register | `BFetch::on_branch_decode`, called for every branch at fetch + `DQ_LATENCY` | |
| Branch Lookahead stage | `BFetch::run_lookahead` | one lookahead engine, see choice 1 |
| Branch Trace Cache, 256 entries | `brtc`, `brtc_update`, `brtc_lookup` | updated at commit, as in the paper |
| Path confidence estimator, 2048 entries | `PathConfidence` | see choice 6 |
| Alternate Register File | `record_write`, `read_arf` | see choice 5 |
| Memory History Table, 128 entries x 3 registers | `mht`, `learn_load`, `generate` | RegIdx, RegVal, Offset, LoopDelta, LoopCnt, posPatt, negPatt as in Figure 6 |
| Prefetch Calculate, equation 3 | `generate` | address = ARF value + Offset + LoopCnt x LoopDelta |
| Per-load filter, 3 x 2048 3-bit counters, threshold 3 | `filter`, `filter_allows`, `filter_feedback` | feedback comes from the L1D, see choice 7 |
| Prefetch queue, 100 entries | `queue`, `issue`, `put_back` | issued through the simulator's load/store lanes like the stride prefetcher |
| Tournament branch predictor, 6.55 KB | `TournamentPredictor`, about 6.1 KB | Alpha 21264 style |

## Choices we made

1. **One lookahead engine.** A branch starts a lookahead only if the engine is idle when the branch decodes. The engine stays busy for `ceil(depth / width)` cycles. The paper has a single DBR and does not say what happens to branches that arrive while a lookahead is running. Our rule is equivalent to the DBR accepting a new branch only when the engine is free.
2. **Lookahead width.** The paper's lookahead predicts one branch per cycle on a 4-wide core and assumes the predictor can serve as many predictions per cycle as the fetch width. The CBP2025 core fetches 16 instructions and up to 16 branches per cycle, so one branch per cycle can never get ahead of fetch. We make the width a knob and use 4 branches per cycle in the main experiments.
3. **Table sizes.** With the paper's 256-entry BrTC and 128-entry MHT, lookaheads on these traces stop after about 4 blocks because the BrTC misses. The paper was sized for SPEC CPU2006, whose code footprints are much smaller than the server-like CBP2025 traces. The main experiments use a 4096-entry BrTC and a 2048-entry MHT, and we report the paper's sizes as a sensitivity point.
4. **RegVal comes from the ARF at the leading branch's decode.** The paper says RegVal "is read into the MHT from the ARF". We snapshot the ARF when each branch decodes and learn `Offset = EA - RegVal` against the snapshot of the branch that led into the load's block. The ARF lags fetch by the fetch-to-execute distance, which is at least 10 cycles of 16-wide fetch here. Offsets learned this way absorb that lag, and the lookahead reads the same lagging ARF, so it cancels. Our first version used precise commit-time register values instead, and 94% of its prefetches went to blocks that were already in the cache.
5. **ARF timing.** Every register write is recorded with the cycle its value is produced. A read at cycle t returns the youngest write produced by t, which is the paper's rule of accepting only younger updates. We keep the last 32 writes per integer register.
6. **Path confidence.** The paper combines JRS, up-down and self counters (Jimenez) with the path confidence method of Malik et al. We use one 4-bit JRS resetting counter per entry and, as in Malik et al., the measured correctness rate of each counter value as that branch's probability of being right. Path confidence is the product along the path, and the lookahead stops below 0.75 (Table II). The estimator is trained with the correctness of whichever predictor drives the lookahead.
7. **Per-load filter.** Each of a load's three counters starts at 2, so their sum starts at 6 and a load is blocked after a net two useless prefetches, when the sum falls below the paper's threshold of 3. Each L1D block remembers whether a prefetch brought it in and for which load. A first demand hit counts as useful and an eviction before any use counts as useless, as in the paper. The paper does not say how a blocked load comes back. We follow it literally: a blocked load sends no prefetches, so it gets no feedback and stays blocked. This is the default and run v5 uses it. On these traces it blocks almost every load early, useful ones included: on the 16 traces where the oracle gained most in v2, oracle-driven B-Fetch falls from +23% to +4% on geometric mean (`results/noprobe_test`). Runs v1 and v2 tested a probe that is not in the paper: one blocked candidate in 16 or in 64 still went out, so a blocked load could earn its way back (`-K probe=N`, `results/filter`). We keep those runs only as a diagnostic.
8. **Duplicates.** Our addition: a candidate is dropped if the same block is already queued or was queued among the last 64 prefetches. Consecutive lookaheads overlap almost completely, and without this most of the queue would be repeats.
9. **Issue window.** A queued prefetch may use any free load/store slot up to 32 cycles after it was generated, including cycles in which fetch is stalled, and is dropped after that. The stride prefetcher keeps its original rule.
10. **Targets.** Taken targets come from a 4096-entry BTB learned at fetch. The oracle also knows indirect targets.
11. **Lookahead predictors.**
    - `oracle` follows the true path read ahead from the trace.
    - `tournament` is B-Fetch's own predictor.
    - `core` uses the simulator's conditional branch predictor read-only: TAGE-SC-L in `cbp`, RUNLTS in `cbp_runlts`. A lookahead copies the predictor's speculative history, predicts with the copy and advances the copy, and never trains a table. RUNLTS's RBias sees the register values that are ready when the lookahead starts, so they may be stale for branches further ahead.
12. **Training timing.** The BrTC and MHT learn at commit, as in the paper. The tournament predictor and the confidence estimator learn at fetch with the branch's outcome, after any lookahead that branch starts.
13. **Insertion position.** Prefetched blocks enter the caches at the MRU position like demand fills, as in the paper. `-K l1pflru=1` inserts them at LRU in the L1D and `-K llcpflru=1` in the L2 and L3. In side tests (`results/mitig`) LRU insertion in the L1D removes most of the slowdown on web_14 and int_30, and in the L2 and L3 it helps infra_2, but both cost a few points on traces where B-Fetch helps, so the main runs keep MRU insertion.

## How a lookahead is checked against the truth

With B-Fetch on, `cbp.cc` reads the trace ahead of the simulated instruction and keeps the next branches with their outcomes and the loads that follow each one. Timing is unaffected. While a lookahead is still on the true path we count:

- whether each conditional branch prediction was right (`Lookahead cond predictions correct`);
- how many blocks it visited on and off the true path;
- for each primary candidate whose load really executes in that block instance, whether the predicted block equals that load's real block (`same block`) or is within 4 blocks (`within 4`).

These numbers separate prefetches lost to wrong paths from prefetches lost to wrong addresses.

## All changes to the CBP2025 simulator

| File | Change |
|---|---|
| `lib/cbp.cc` | flags `-N` (stride prefetcher off), `-B`, `-C`, `-L`, `-K`, `-W`; reads the trace ahead when B-Fetch is on |
| `lib/uarchsim.cc` | `-b` no longer skips the branch predictor, which crashed TAGE-SC-L: the predictor is always called and trained, and the core ignores its mispredictions; B-Fetch hooks at fetch, decode and commit; the prefetch drain loop is shared by both prefetchers |
| `lib/cache.h`, `lib/cache.cc` | L1D prefetch accounting: useful, late, useless, redundant; per-load feedback for B-Fetch; optional main memory bandwidth limit (`-W`); optional LRU insertion of prefetches (`l1pflru`, `llcpflru`); average demand latency per level |
| `lib/bfetch.h`, `lib/core_lookahead.h` | new: B-Fetch and the lookahead interface to the core predictor; `-K` knobs `brtc`, `mht`, `width`, `filter`, `filterinit`, `filterthr`, `probe` |
| `cbp2016_tage_sc_l.h` | `HistoryUpdate` works on any history copy; three read-only lookahead methods |
| `predictors/runlts/` | RUNLTS from its CBP2025 release, with the same two changes |
| `Makefile` | builds `cbp` (TAGE-SC-L) and `cbp_runlts` |

Without the new flags every run gives exactly the same result as the original simulator. We check this on `media_0` (IPC 5.9117) and on the sample int trace (264 mispredictions).

## Shadow scoring of the core predictor

With `-B oracle -K shadow=1`, every oracle lookahead also asks the core predictor, read-only, to predict each conditional branch on the true path, and pushes the true outcome into its lookahead history. TAGE-SC-L (`cbp`) and RUNLTS (`cbp_runlts`) are then scored on exactly the same branches at each lookahead depth, which the normal `-B core` runs cannot do, because each predictor's own confidence decides how deep its lookahead goes. `shadow=2` also overwrites RUNLTS's lookahead register values with the true values written along the path, which bounds what projecting register values for RBias could gain. Shadow mode does not change timing or prefetches. It changes TAGE-SC-L's own fetch-time mispredictions by about 0.04% and RUNLTS's not at all.

## Known issue: RUNLTS depends on the binary when B-Fetch is on

With B-Fetch off, every build of `cbp_runlts` gives the same result. With B-Fetch on and identical core timing, RUNLTS's own fetch-time mispredictions on web_21 are 26240 in some builds and 27775 in others, about 6% apart. Each binary is deterministic from run to run. AddressSanitizer and UBSan report nothing, and `-O2` versus `-O3` or `-fno-strict-aliasing` move the result in ways that do not repeat across rebuilds, so the likeliest cause is an uninitialized read inside RUNLTS. It does not touch IPC directly, because the core predicts perfectly, and RUNLTS's fetch-time accuracy is taken from runs without B-Fetch. It can shift the RUNLTS lookahead results by a small amount, so RUNLTS numbers should always be compared within one binary.
