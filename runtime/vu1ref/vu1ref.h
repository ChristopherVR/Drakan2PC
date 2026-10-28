// Standalone VU1 reference simulator (PCSX2-interpreter semantics: VUops.cpp / VUflags) for the Drakan microprogram.
// Independent of the runtime's interpreter on purpose.  Cycle model: 1 pair/cycle + interlock stalls;
// FMAC results/flags visible 4 cycles after issue (flags are NOT interlocked), DIV/Q 7 cycles (WAITQ stalls), ERLENG/P
// 24, ILW 4 cycles for branch readers, VI branch hazard window of 2 cycles (reader sees the OLD value), CLIP shift
// register.
#pragma once
#include <cstdint>
#include <cstring>
#include <vector>
#include <string>

struct Vu1RefOptions {
    int flagLatency = 4; // cycles until MAC/CLIP results become visible
    int divLatency = 7;
    int fmacLatency = 4;
    bool branchHazard = true;
    bool stallModel = true; // false: never stall (pure 1 cycle/pair)
    bool macFromMaxMini = false;
    bool trace = false;
};

struct Vu1Ref {
    uint32_t vf[32][4];
    uint16_t vi[16];
    uint32_t acc[4];
    uint32_t q, p, i, r;
    uint32_t mac, clip, status; // architecturally visible values
    uint32_t clipRun;           // issue-order clip shift register
    uint8_t code[0x4000];
    uint8_t mem[0x4000];
    uint32_t top, itop;
    uint32_t pc;
    uint64_t cycle;
    Vu1RefOptions opt;
    // statistics
    uint64_t nStalls = 0, nPairs = 0, nQEarly = 0, nHazard = 0;
    std::vector<std::vector<uint8_t>> packets; // XGKICK snapshots
    std::vector<uint32_t> kicks;
    std::vector<uint32_t> kickPcs; // pc of the XGKICK pair
    std::string error;

    Vu1Ref();
    void reset();
    // run from pc until the E-bit program end (delay slot executed).  Returns false on error / budget.
    bool run(uint32_t startPc, uint32_t maxPairs = 200000);

private:
    struct FmacEnt {
        uint64_t ready;
        uint32_t mac, clip;
        bool hasMac, hasClip;
    };
    std::vector<FmacEnt> flagPipe;
    uint64_t vfReady[32][4];
    bool qPend = false;
    uint64_t qReady = 0;
    uint32_t qVal = 0;
    bool pPend = false;
    uint64_t pReady = 0;
    uint32_t pVal = 0;
    uint64_t iluReady[16];
    int backupReg = -1;
    uint16_t backupVal = 0;
    uint64_t backupUntil = 0;
    void settle();
    void flushAll();
    uint16_t readBranchVi(int reg);
    void writeVi(int reg, uint16_t v);
};
