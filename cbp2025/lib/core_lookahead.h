#pragma once
// Read-only lookahead access to the simulator's own conditional branch predictor, used by
// B-Fetch's lookahead. begin() copies the predictor's speculative history, predict() reads the
// tables with that copy, push() advances the copy with a predicted branch. Nothing is trained.

#include <cstdint>

struct CoreLookahead
{
    virtual void begin() = 0;
    virtual bool predict(uint64_t pc) = 0;
    virtual void push(uint64_t pc, bool cond, bool indirect, bool taken, uint64_t target) = 0;
    // Overwrite one register value in the lookahead copy. Only predictors that read register
    // values, like RUNLTS, need it.
    virtual void set_reg(uint64_t reg, uint64_t value) {}
    virtual ~CoreLookahead() = default;
};

// Defined next to the predictor, in cond_branch_predictor_interface.cc.
CoreLookahead *get_core_lookahead();
