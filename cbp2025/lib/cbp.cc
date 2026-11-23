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


#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <assert.h>
#include <string.h>
#include "cbp.h"
#include "trace_reader.h"
#include "fifo.h"
#include "cache.h"
#include "bp.h"
#include "resource_schedule.h"
#include "uarchsim.h"
#include "parameters.h"

uarchsim_t *sim;

int parseargs(int argc, char ** argv) 
{
  int i = 1;

  // read optional flags
  while (i < argc)
  {
     if (!strcmp(argv[i], "-d"))
     {
        PERFECT_CACHE = true;
        i++;
     }
     else if (!strcmp(argv[i], "-b"))
     {
        PERFECT_BRANCH_PRED = true;
        i++;
     }
     //else if (!strcmp(argv[i], "-i"))
     //{
     //   PERFECT_INDIRECT_PRED = true;
     //   i++;
     //}
     else if (!strcmp(argv[i], "-P"))
     {
        PREFETCHER_ENABLE = true;
        i++;
     }
     else if (!strcmp(argv[i], "-N"))
     {
        PREFETCHER_ENABLE = false;   // the stride prefetcher is on by default
        i++;
     }
     else if (!strcmp(argv[i], "-W"))
     {
        i++;
        if (i < argc)
        {
           MEM_CYCLES_PER_BLOCK = atoi(argv[i]);
           i++;
        }
        else
        {
           printf("Usage: missing memory bandwidth limit: -W <cycles per L3 block transfer>.\n");
           exit(0);
        }
     }
     else if (!strcmp(argv[i], "-B"))
     {
        i++;
        const char *names[] = {"oracle", "tournament", "core"};
        int mode = -1;
        for (int m = 0; m < 3; m++)
           if (i < argc && !strcmp(argv[i], names[m])) mode = m;
        if (mode < 0)
        {
           printf("Usage: -B <oracle|tournament|core> enables B-Fetch with that lookahead predictor.\n");
           exit(0);
        }
        BFETCH_ENABLE = true;
        BFETCH_PRED = mode;
        i++;
     }
     else if (!strcmp(argv[i], "-C"))
     {
        i++;
        if (i < argc)
        {
           BFETCH_CONF_THRESHOLD = atof(argv[i]);
           i++;
        }
        else
        {
           printf("Usage: missing B-Fetch path confidence threshold: -C <threshold>.\n");
           exit(0);
        }
     }
     else if (!strcmp(argv[i], "-K"))
     {
        // B-Fetch knobs as key=value pairs separated by commas, e.g. brtc=4096,mht=2048,width=4,filter=0
        i++;
        if (i >= argc)
        {
           printf("Usage: missing B-Fetch knobs: -K brtc=<n>,mht=<n>,width=<n>,filter=<0|1>.\n");
           exit(0);
        }
        std::string knobs(argv[i]);
        size_t start = 0;
        while (start < knobs.size())
        {
           size_t end = knobs.find(',', start);
           if (end == std::string::npos) end = knobs.size();
           const std::string kv = knobs.substr(start, end - start);
           const size_t eq = kv.find('=');
           const std::string key = kv.substr(0, eq);
           const uint64_t val = (eq == std::string::npos) ? 0 : strtoull(kv.c_str() + eq + 1, nullptr, 10);
           if (key == "brtc") BFETCH_BRTC_ENTRIES = val;
           else if (key == "mht") BFETCH_MHT_ENTRIES = val;
           else if (key == "width") BFETCH_LA_WIDTH = val;
           else if (key == "filter") BFETCH_FILTER = (val != 0);
           else if (key == "filterinit") BFETCH_FILTER_INIT = val;
           else if (key == "filterthr") BFETCH_FILTER_THRESHOLD = val;
           else if (key == "probe") BFETCH_PROBE_PERIOD = val;
           else if (key == "shadow") BFETCH_SHADOW = val;
           else if (key == "l1pflru") PF_FILL_LRU_L1 = (val != 0);
           else if (key == "llcpflru") PF_FILL_LRU_LLC = (val != 0);
           else
           {
              printf("Unknown B-Fetch knob '%s'. Known: brtc, mht, width, filter, filterinit, filterthr, probe, shadow, l1pflru, llcpflru.\n", key.c_str());
              exit(0);
           }
           start = end + 1;
        }
        i++;
     }
     else if (!strcmp(argv[i], "-L"))
     {
        i++;
        if (i < argc)
        {
           BFETCH_MAX_DEPTH = atoi(argv[i]);
           i++;
        }
        else
        {
           printf("Usage: missing B-Fetch maximum lookahead depth: -L <branches>.\n");
           exit(0);
        }
     }
     //else if (!strcmp(argv[i], "-f"))
     //{
     //   i++;
     //   if (i < argc)
     //   {
     //      PIPELINE_FILL_LATENCY = atoi(argv[i]);
     //      i++;
     //   }
     //   else
     //   {
     //      printf("Usage: missing pipeline fill latency: -f <pipeline_fill_latency>.\n");
     //      exit(0);
     //   }
     //}
     else if (!strcmp(argv[i], "-M"))
     {
        i++;
        if (i < argc)
        {
           NUM_LDST_LANES = atoi(argv[i]);
           i++;
        }
        else
        {
           printf("Usage: missing # load/store lanes: -M <num_ldst_lanes>.\n");
           exit(0);
        }
     }
     else if (!strcmp(argv[i], "-A"))
     {
        i++;
        if (i < argc)
        {
           NUM_ALU_LANES = atoi(argv[i]);
           i++;
        }
        else
        {
           printf("Usage: missing # alu lanes: -A <num_alu_lanes>.\n");
           exit(0);
        }
     }
     //else if (!strcmp(argv[i], "-s")) {
     //   WRITE_ALLOCATE = true;
     // i++;
     //}
     else if (!strcmp(argv[i], "-F"))
     {
        i++;
        if (i < argc)
        {
           unsigned int temp1, temp2, temp3, temp4, temp5;
           if (sscanf(argv[i], "%u,%u,%u,%u,%u", &temp1, &temp2, &temp3, &temp4, &temp5) == 5)
           {
              FETCH_WIDTH = (uint64_t)temp1;
              FETCH_NUM_BRANCH = (uint64_t)temp2;
              FETCH_STOP_AT_INDIRECT = (temp3 ? true : false);
              FETCH_STOP_AT_TAKEN = (temp4 ? true : false);
              FETCH_MODEL_ICACHE = (temp5 ? true : false);
           }
           else
           {
              printf("Usage: missing one or more fetch bundle constraints: -F <fetch_width>,<fetch_num_branch>,<fetch_stop_at_indirect>,<fetch_stop_at_taken>,<fetch_model_icache>.\n");
              exit(0);
           }
           i++;
        }
        else
        {
           printf("Usage: missing one or more fetch bundle constraints: -F <fetch_width>,<fetch_num_branch>,<fetch_stop_at_indirect>,<fetch_stop_at_taken>,<fetch_model_icache>.\n");
           exit(0);
        }
     }
     else if (!strcmp(argv[i], "-I"))
     {
        i++;
        if (i < argc)
        {
           unsigned int temp1, temp2, temp3;
           if (sscanf(argv[i], "%u,%u,%u", &temp1, &temp2, &temp3) == 3)
           {
              IC_SIZE = (uint64_t)(1 << temp1);
              IC_ASSOC = (uint64_t)temp2;
              IC_BLOCKSIZE = (uint64_t)temp3;
           }
           else
           {
              printf("Usage: missing one or more I$ parameters: -I <log2_size>,<assoc>,<blocksize>.\n");
              exit(0);
           }
           i++;
        }
        else
        {
           printf("Usage: missing I$ parameters: -I <log2_size>,<assoc>,<blocksize>.\n");
           exit(0);
        }
     }
     else if (!strcmp(argv[i], "-D"))
     {
        i++;
        if (i < argc)
        {
           unsigned int temp1, temp2, temp3, temp4, temp5, temp6, temp7, temp8, temp9, temp10, temp11, temp12, temp13;
           if (sscanf(argv[i], "%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u",
                      &temp1, &temp2, &temp3, &temp4,
                      &temp5, &temp6, &temp7, &temp8,
                      &temp9, &temp10, &temp11, &temp12,
                      &temp13) == 13)
           {
              L1_SIZE = (uint64_t)(1 << temp1);
              L1_ASSOC = (uint64_t)temp2;
              L1_BLOCKSIZE = (uint64_t)temp3;
              L1_LATENCY = (uint64_t)temp4;

              L2_SIZE = (uint64_t)(1 << temp5);
              L2_ASSOC = (uint64_t)temp6;
              L2_BLOCKSIZE = (uint64_t)temp7;
              L2_LATENCY = (uint64_t)temp8;

              L3_SIZE = (uint64_t)(1 << temp9);
              L3_ASSOC = (uint64_t)temp10;
              L3_BLOCKSIZE = (uint64_t)temp11;
              L3_LATENCY = (uint64_t)temp12;

              MAIN_MEMORY_LATENCY = (uint64_t)temp13;
           }
           else
           {
              printf("Usage: missing one or more L1$, L2$, and L3$ parameters: -D <log2_L1_size>,<L1_assoc>,<L1_blocksize>,<L1_latency>,<log2_L2_size>,<L2_assoc>,<L2_blocksize>,<L2_latency>,<log2_L3_size>,<L3_assoc>,<L3_blocksize>,<L3_latency>,<main_memory_latency>.\n");
              exit(0);
           }
           i++;
        }
        else
        {
           printf("Usage: missing L1$, L2$, and L3$ parameters: -D <log2_L1_size>,<L1_assoc>,<L1_blocksize>,<L1_latency>,<log2_L2_size>,<L2_assoc>,<L2_blocksize>,<L2_latency>,<log2_L3_size>,<L3_assoc>,<L3_blocksize>,<L3_latency>,<main_memory_latency>.\n");
           exit(0);
        }
     }
     else if (!strcmp(argv[i], "-E"))
     {
        i++;
        PRINT_PER_EPOCH_STATS = true;
        if (i < argc)
        {
           uint64_t epoch_size_insts;
           if (sscanf(argv[i], "%lu", &epoch_size_insts) == 1)
           {
              EPOCH_SIZE_INSTS = epoch_size_insts;
           }
           else
           {
              printf("Usage: missing epoch size: -E <epoch_size>\n");
              exit(0);
           }
           i++;
        }
        else
        {
           printf("Usage: missing epoch size: -E <epoch_size>\n");
           exit(0);
        }
     }
     else if (!strcmp(argv[i], "-w"))
     {
        i++;
        if (i < argc)
        {
           WINDOW_SIZE = atoi(argv[i]);
           i++;
        }
        else
        {
           printf("Usage: missing window size: -w <window_size>.\n");
           exit(0);
        }
     }
     else
     {
        break;
     }
  }

  if (i < argc) {
     return(i);
  }
  else {
     //printf("usage:\t%s\n[optional: -d to enable perfect data cache]\n\t[optional: -b to enable perfect branch prediction (all branch types)]\n\t[optional: -i to enable perfect indirect-branch prediction]\n\t[optional: -P to enable stride prefetcher in L1D]\n\t[optional: -f <pipeline_fill_latency>]\n\t[optional: -M <num_ldst_lanes>\n\t[optional: -A <num_alu_lanes>\n\t[optional: -F <fetch_width>,<fetch_num_branch>,<fetch_stop_at_indirect>,<fetch_stop_at_taken>,<fetch_model_icache>]\n\t[optional: -I <log2_ic_size>,<ic_assoc>,<ic_blocksize>]\n\t[optional: -D <log2_L1_size>,<L1_assoc>,<L1_blocksize>,<L1_latency>,<log2_L2_size>,<L2_assoc>,<L2_blocksize>,<L2_latency>,<log2_L3_size>,<L3_assoc>,<L3_blocksize>,<L3_latency>,<main_memory_latency>]\n\t[optional: -w <window_size>]\n\t[REQUIRED: .gz trace file]\n\t[optional: contestant's arguments]\n", argv[0]);
     printf("usage:\t%s\n"
             //"\t[optional: -v to enable value prediction]\n", 
             //"\t[optional: -p to enable perfect value prediction (if -v also specified)]\n",
             "\t[optional: -d to enable perfect data cache]\n"
             "\t[optional: -b to enable perfect branch prediction (all branch types)]\n"
             // "\t[optional: -i to enable perfect indirect-branch prediction]\n"
             "\t[optional: -P to enable stride prefetcher in L1D (on by default)]\n"
             "\t[optional: -N to disable the stride prefetcher]\n"
             "\t[optional: -W <cycles> main memory bandwidth, one L3 block transfer per <cycles>, default unlimited]\n"
             "\t[optional: -B <oracle|tournament|core> to enable B-Fetch with that lookahead predictor]\n"
             "\t[optional: -C <threshold> B-Fetch path confidence threshold, default 0.75]\n"
             "\t[optional: -L <branches> B-Fetch maximum lookahead depth, default 32]\n"
             "\t[optional: -K brtc=<n>,mht=<n>,width=<n>,filter=<0|1>,filterinit=<n>,filterthr=<n>,probe=<n>,shadow=<0|1|2>,l1pflru=<0|1>,llcpflru=<0|1>\n"
             "\t           B-Fetch table sizes, lookahead branches per cycle, per-load filter and its initial counter value,\n"
             "\t           and whether prefetched blocks are inserted at LRU in the L1D and in the L2/L3]\n"
             // "\t[optional: -f <pipeline_fill_latency>]\n"
             "\t[optional: -M <num_ldst_lanes>\n"
             "\t[optional: -A <num_alu_lanes>\n"
             "\t[optional: -F <fetch_width>,<fetch_num_branch>,<fetch_stop_at_indirect>,<fetch_stop_at_taken>,<fetch_model_icache>]\n"
             "\t[optional: -I <log2_ic_size>,<ic_assoc>,<ic_blocksize>]\n"
             "\t[optional: -D <log2_L1_size>,<L1_assoc>,<L1_blocksize>,<L1_latency>,<log2_L2_size>,<L2_assoc>,<L2_blocksize>,<L2_latency>,<log2_L3_size>,<L3_assoc>,<L3_blocksize>,<L3_latency>,<main_memory_latency>]\n"
             "\t[optional: -w <window_size>]\n"
             "\t[optional: -E <epoch_size_insts> to enable dumping per-epoch conditional branch info\n"
             "\t[REQUIRED: .gz trace file]\n", argv[0]);
     exit(0);
  }
}

int main(int argc, char ** argv)
{
  int i = parseargs(argc, argv);
  TraceReader reader(argv[i]);

  // Need to create simulator after parsing arguments (for global parameters).
  sim = new uarchsim_t;
 
  // Get to next (optional) argument after trace filename.
  i++;
  //if (i < argc)
  //   beginPredictor((argc - i), &(argv[i]));
  //else
  //   beginPredictor(0, (char **)NULL);

  //if (i < argc)
  //   beginCondDirPredictor((argc - i), &(argv[i]));
  //else
  //   beginCondDirPredictor(0, (char **)NULL);
  beginCondDirPredictor();

  // With B-Fetch on, read the trace ahead of the simulated instruction so that brecs holds the
  // next branch to be simulated and enough true branches after it for the oracle lookahead and
  // for checking every lookahead against the real path. Timing is unaffected.
  std::deque<db_t *> ahead;
  std::deque<BranchRecord> brecs;
  const size_t branches_needed = BFETCH_ENABLE ? (BFETCH_MAX_DEPTH + 2) : 0;
  bool trace_done = false;
  auto refill = [&]() {
     while (!trace_done && (ahead.empty() || brecs.size() < branches_needed))
     {
        db_t *next = reader.get_inst();
        if (next == nullptr) { trace_done = true; break; }
        ahead.push_back(next);
        if (!BFETCH_ENABLE) continue;
        if (is_br(next->insn_class))
        {
           BranchRecord r;
           r.pc = next->pc;
           r.next_pc = next->next_pc;
           r.taken = next->is_taken;
           r.cond = is_cond_br(next->insn_class);
           r.indirect = is_uncond_ind_br(next->insn_class);
           brecs.push_back(std::move(r));
        }
        else if (next->is_load && !brecs.empty())
           brecs.back().loads.emplace_back(next->pc, next->addr);
        // Shadow mode 2 needs the register values written between branches, a branch's own
        // write included, since it lands after the branch is predicted.
        if (BFETCH_SHADOW == 2 && next->D.valid && !brecs.empty())
           brecs.back().writes.emplace_back(next->D.log_reg, next->D.value);
     }
  };
  sim->set_branch_records(&brecs);
  refill();
  db_t *inst = ahead.empty() ? nullptr : ahead.front();

  //bool dump_activity = true;
  //uint64_t current_fetch_cycle = 0;
  while (inst != nullptr)
  {
      ahead.pop_front();
      //const bool logging_activated = (LOG_LEVEL != 0) && (current_fetch_cycle>= LOG_START_CYCLE) && (current_fetch_cycle<=LOG_END_CYCLE);
      //if(logging_activated && dump_activity)
      //{
      //    std::cout<<"======================================================= Begin "<<current_fetch_cycle<<"=======================================================\n";
      //    dump_activity = false;
      //}

      sim->step(inst);

      //const uint64_t next_fetch_cycle = sim->get_current_fetch_cycle();
      //if(logging_activated && next_fetch_cycle != current_fetch_cycle)
      //{
      //    dump_activity = true;
      //    std::cout<<"======================================================= End "<<current_fetch_cycle<<"->"<<next_fetch_cycle<<"=======================================================\n";
      //}
      //current_fetch_cycle = next_fetch_cycle;
      if (BFETCH_ENABLE && is_br(inst->insn_class))
         brecs.pop_front();   // this branch is behind us now
      delete inst;
      refill();
      inst = ahead.empty() ? nullptr : ahead.front();
  }

  endPredictor();
  endCondDirPredictor();
  sim->output();
}
