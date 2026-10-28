#ifndef PS2_VU1_H
#define PS2_VU1_H

#include <array>
#include <cstdint>
#include <cstring>
#include <emmintrin.h>
#include <vector>

class GS;
class PS2Memory;

// Vector form of VU1Interpreter::normalizeOperand: denormals -> signed zero, exponent 255 -> signed largest finite.
#if defined(_MSC_VER)
__forceinline
#else
__attribute__((always_inline)) inline
#endif
    static __m128
    vuNormalizeVec(__m128 v) {
    const __m128i bits = _mm_castps_si128(v);
    const __m128i sign = _mm_and_si128(bits, _mm_set1_epi32(static_cast<int>(0x80000000u)));
    const __m128i exponent = _mm_and_si128(_mm_srli_epi32(bits, 23), _mm_set1_epi32(0xFF));
    const __m128i isZero = _mm_cmpeq_epi32(exponent, _mm_setzero_si128());
    const __m128i isMax = _mm_cmpeq_epi32(exponent, _mm_set1_epi32(0xFF));
    __m128i out = _mm_andnot_si128(_mm_or_si128(isZero, isMax), bits);
    out = _mm_or_si128(out, _mm_and_si128(isZero, sign));
    out = _mm_or_si128(out, _mm_and_si128(isMax, _mm_or_si128(sign, _mm_set1_epi32(0x7F7FFFFF))));
    return _mm_castsi128_ps(out);
}

struct VU1State {
    float vf[32][4];
    int32_t vi[16];
    float acc[4];
    float q;
    float p;
    float i;
    uint32_t r;
    uint32_t pc;
    uint32_t mac;
    uint32_t clip;
    uint32_t status;
    uint64_t cycles;
    bool ebit;
    bool haltAfterDelaySlot;
    bool dBitEnabled;
    bool tBitEnabled;
    bool stoppedByD;
    bool stoppedByT;
    uint32_t top;  // VIF TOP visible to XTOP
    uint32_t itop; // VIF ITOP visible to XITOP

    bool branchPending;
    uint32_t branchTarget;
    uint32_t branchDelay;
};

class VU1Interpreter {
public:
    enum class Unit : uint8_t { VU0, VU1 };

    friend struct Vu1Fast; // file-local helpers (vu/*.cpp) with access to the private state
    friend struct Vu1Decode;
    friend struct Vu1Step;
    friend struct Vu1Batch;

    explicit VU1Interpreter(Unit unit = Unit::VU1);

    void reset();

    void execute(uint8_t* vuCode, uint32_t codeSize, uint8_t* vuData, uint32_t dataSize, GS& gs,
                 PS2Memory* memory = nullptr, uint32_t startPC = 0, uint32_t top = 0, uint32_t itop = 0,
                 uint32_t maxCycles = 65536);

    void resume(uint8_t* vuCode, uint32_t codeSize, uint8_t* vuData, uint32_t dataSize, GS& gs,
                PS2Memory* memory = nullptr, uint32_t top = 0, uint32_t itop = 0, uint32_t maxCycles = 65536);

    // Debug/test: when set, every packet XGKICK sends to PATH1 is also appended here.
    void setPacketTap(std::vector<std::vector<uint8_t>>* tap) { m_packetTap = tap; }
    // Perf counters (cycles executed / program runs since reset).
    uint64_t totalCycles() const { return m_cycle; }

    VU1State& state() { return m_state; }
    const VU1State& state() const { return m_state; }

private:
    enum Pipeline : uint8_t {
        PipelineNone = 0,
        PipelineFmac,
        PipelineLsu,
        PipelineFdiv,
        PipelineEfu,
        PipelineIalu,
        PipelineBranch,
        PipelineXgkick
    };

    struct VfAccess {
        uint8_t reg = 0;
        uint8_t lanes = 0;
    };

    struct InstructionUsage {
        std::array<VfAccess, 2> vfRead{};
        VfAccess vfWrite{};
        uint8_t vfReadCount = 0;
        uint16_t viRead = 0;
        uint16_t viWrite = 0;
        uint8_t accRead = 0;
        uint8_t accWrite = 0;
        uint8_t latency = 0;
        uint8_t vfLatency = 0;
        uint8_t viLatency = 0;
        Pipeline pipeline = PipelineNone;
        bool waitQ = false;
        bool waitP = false;
        bool readsClip = false;
        bool writesClip = false;
        bool delaysNextBranchRead = false;
        bool reserved = false;
    };

    struct DecodedInstructionPair {
        uint32_t lower = 0;
        uint32_t upper = 0;
        InstructionUsage lowerUsage{};
        InstructionUsage upperUsage{};
        bool iBit = false;
        bool eBit = false;
        bool mBit = false;
        bool dBit = false;
        bool tBit = false;
        uint8_t upperVfShadowReg = 0;
        uint8_t suppressedLowerVf = 0;
        // Perf: hazard summary of both halves (see calculatePairReadyCycle) ...
        uint8_t rdCount = 0;
        std::array<uint8_t, 4> rdReg{};
        std::array<uint8_t, 4> rdLanes{};
        uint16_t viReadMask = 0;
        uint8_t accReadMask = 0;
        // ... and the arithmetic upper-unit fast path (execUpperFmac): fmKind 0 = not an FMAC arithmetic op.
        // VF/VI/ACC results this pair produces, with their latencies (markPairWrites)
        uint8_t wrCount = 0;
        std::array<uint8_t, 2> wrReg{};
        std::array<uint8_t, 2> wrLanes{};
        std::array<uint8_t, 2> wrLat{};
        uint16_t viWriteMask = 0;
        uint8_t viWriteLat = 0;
        uint8_t accWriteMask = 0;
        // Register-busy model (see ps2_vu1_fast.h): bit masks of the registers this pair reads, and groups of registers
        // it writes with their latency. bit = (reg & 15) * 4 + component in lo (reg < 16) / hi (reg >= 16); va: VI
        // register i = bit i, ACC component c = bit 16 + c.
        uint64_t rdLo = 0, rdHi = 0;
        uint32_t rdVa = 0;
        uint8_t grpCount = 0;
        bool needsExtra = false; // FDIV / EFU / XGKICK / WAITQ / WAITP resources (calculatePairReadyCycle)
        std::array<uint64_t, 4> grpLo{}, grpHi{};
        std::array<uint32_t, 4> grpVa{};
        std::array<uint8_t, 4> grpLat{};
        uint8_t fmKind = 0; // 1 ADD, 2 SUB, 3 MUL, 4 MADD, 5 MSUB
        uint8_t fmB = 0;    // 0..3 broadcast component of FT, 4 Q, 5 I, 6 FT lane-wise
        bool fmToAcc = false;
    };

    struct FlagPipelineEntry {
        uint64_t readyCycle = 0;
        uint64_t issueCycle = 0;
        uint32_t mac = 0;
        uint32_t status = 0;
        uint32_t extraSticky = 0;
        uint32_t clip = 0;
        bool valid = false;
        bool writesMac = false;
        bool writesStatus = false;
        bool writesSticky = false;
        bool writesClip = false;
    };

    struct ScalarPipelineEntry {
        uint64_t readyCycle = 0;
        float value = 0.0f;
        uint32_t statusDi = 0;
        bool valid = false;
    };

    struct PendingStore {
        uint64_t readyCycle = 0;
        uint32_t address = 0;
        std::array<uint32_t, 4> words{};
        uint8_t laneMask = 0;
        bool valid = false;
    };

    struct PendingVfWrite {
        uint64_t readyCycle = 0;
        uint64_t sequence = 0;
        std::array<float, 4> value{};
        uint8_t reg = 0;
        uint8_t laneMask = 0;
        bool valid = false;
    };

    struct PendingViWrite {
        uint64_t readyCycle = 0;
        uint64_t sequence = 0;
        int32_t value = 0;
        uint8_t reg = 0;
        bool valid = false;
    };

    struct PendingAccWrite {
        uint64_t readyCycle = 0;
        uint64_t sequence = 0;
        std::array<float, 4> value{};
        uint8_t laneMask = 0;
        bool valid = false;
    };

    struct XgkickPipeline {
        static constexpr uint32_t kBufferSize = 0x10000u;
        std::array<uint8_t, kBufferSize> packet{};
        uint32_t sourceAddress = 0;
        uint32_t totalBytes = 0;
        uint32_t copiedBytes = 0;
        uint32_t currentTagEnd = 0;
        uint32_t cycleCredit = 0;
        uint64_t issueCycle = 0;
        bool active = false;
        bool currentTagEop = false;
    };

    static constexpr uint32_t kFmacLatency = 4u;
    static constexpr uint32_t kAccForwardLatency = 1u;
    static constexpr uint32_t kMaxFlagEntries = 8u;
    static constexpr uint32_t kMaxPendingStores = 8u;
    static constexpr uint32_t kMaxPendingVfWrites = 16u;
    static constexpr uint32_t kMaxPendingViWrites = 8u;
    static constexpr uint32_t kMaxPendingAccWrites = 8u;
    static constexpr uint32_t kMaxDecodedPairs = 0x4000u / 8u;

    Unit m_unit;
    VU1State m_state;
    std::array<DecodedInstructionPair, kMaxDecodedPairs> m_decodedCodeCache{};
    const uint8_t* m_cachedVuCode = nullptr;
    const PS2Memory* m_cachedMemory = nullptr;
    uint32_t m_cachedCodeSize = 0;
    uint64_t m_cachedCodeGeneration = 0;
    bool m_decodedCodeCacheValid = false;

    // Perf: bit masks of the valid entries of each write pipeline (ascending bit order == array order, so commit order
    // and first-free-slot allocation are unchanged), the earliest cycle at which any pending entry becomes ready, and
    // an upper bound of every register-ready cycle.
    uint32_t m_flagMask = 0, m_storeMask = 0, m_vfMask = 0, m_viMask = 0, m_accMask = 0, m_efuMask = 0;
    std::vector<std::vector<uint8_t>>* m_packetTap = nullptr;
    // The upper unit writes its VF / ACC result here instead of into m_state (the writes are queued with their
    // latency).
    float* m_upperDst = nullptr;
    float* m_upperAccDst = nullptr;
    // Spare room so that experiments in vu/*.cpp do not change this header (it is included by every generated TU).
    alignas(64) uint8_t m_spare[4096]{};
    uint64_t m_minReady = ~0ull;
    uint64_t m_maxRegReady = 0;
    DecodedInstructionPair m_adhocDecoded{};
    std::array<FlagPipelineEntry, kMaxFlagEntries> m_flagPipeline{};
    ScalarPipelineEntry m_fdiv{};
    std::array<ScalarPipelineEntry, 2> m_efu{};
    std::array<PendingStore, kMaxPendingStores> m_storePipeline{};
    std::array<PendingVfWrite, kMaxPendingVfWrites> m_vfWritePipeline{};
    std::array<PendingViWrite, kMaxPendingViWrites> m_viWritePipeline{};
    std::array<PendingAccWrite, kMaxPendingAccWrites> m_accWritePipeline{};
    XgkickPipeline m_xgkick{};
    std::array<std::array<uint64_t, 4>, 32> m_vfReady{};
    std::array<uint64_t, 16> m_viReady{};
    std::array<uint64_t, 4> m_accReady{};
    std::array<std::array<uint64_t, 4>, 32> m_vfLatestWrite{};
    std::array<uint64_t, 16> m_viLatestWrite{};
    std::array<uint64_t, 4> m_accLatestWrite{};

    uint64_t m_cycle = 0;
    uint64_t m_nextWriteSequence = 0;
    uint64_t m_efuResourceReady = 0;
    uint32_t m_workingClip = 0;
    uint32_t m_currentUpperInstruction = 0;
    int32_t m_viBranchBackupValue = 0;
    uint8_t m_viBranchBackupReg = 0;
    bool m_viBranchBackupValid = false;
    uint8_t* m_activeVuData = nullptr;
    uint32_t m_activeVuDataSize = 0;
    GS* m_activeGs = nullptr;
    PS2Memory* m_activeMemory = nullptr;
    bool m_stopRequested = false;
    bool m_pendingHaltD = false;
    bool m_pendingHaltT = false;

    void run(uint8_t* vuCode, uint32_t codeSize, uint8_t* vuData, uint32_t dataSize, GS& gs, PS2Memory* memory,
             uint32_t maxCycles);

    InstructionUsage decodeUpperUsage(uint32_t upper) const;
    InstructionUsage decodeLowerUsage(uint32_t lower) const;
    static void addVfRead(InstructionUsage& usage, uint8_t reg, uint8_t lanes);
    static void addVfWrite(InstructionUsage& usage, uint8_t reg, uint8_t lanes);
    static uint8_t vfReadLanes(const InstructionUsage& usage, uint8_t reg);
    DecodedInstructionPair decodeInstructionPair(const uint8_t* vuCode, uint32_t pc) const;
    const DecodedInstructionPair* prepareDecodedCache(const uint8_t* vuCode, uint32_t codeSize, PS2Memory* memory);
    const DecodedInstructionPair& getDecodedInstructionPairForPc(const uint8_t* vuCode, uint32_t codeSize,
                                                                 PS2Memory* memory, uint32_t pc);
    void rebuildDecodedCodeCache(const uint8_t* vuCode, uint32_t codeSize, const PS2Memory* memory,
                                 uint64_t generation);

    void execUpper(uint32_t instr);
    void execUpperFmac(const DecodedInstructionPair& decoded);
    void execLower(uint32_t instr, uint8_t* vuData, uint32_t dataSize, GS& gs, PS2Memory* memory, uint32_t upperInstr);

    void applyDest(float* dst, const float* result, uint8_t dest);
    void applyDestAcc(const float* result, uint8_t dest);
    void applyFmacDest(float* dst, float* result, uint8_t dest);
    void applyFmacDestAcc(float* result, uint8_t dest);
    void normalizeFmacResult(float* result, uint8_t dest, uint8_t laneFlags[4]);
    bool calculateFmacExactResult(uint32_t component, long double& result) const;
    uint8_t normalizeFmacExactResult(float& value, long double exactResult) const;
    uint32_t calculateFmacProductSticky(uint8_t dest) const;
    void updateFmacFlags(const uint8_t laneFlags[4], uint8_t dest, uint32_t extraSticky);
    void queueFsset(uint16_t immediate);
    void queueClip(uint32_t clip);
    void queueFcset(uint32_t clip);
    void queueQ(float value, uint32_t latency, uint32_t statusDi);
    void queueP(float value, uint32_t latency);
    void queueStore(uint32_t address, const uint32_t words[4], uint8_t laneMask);
    void queueVfWrite(uint8_t reg, uint8_t laneMask, const float value[4], uint32_t latency);
    void queueViWrite(uint8_t reg, int32_t value, uint32_t latency);
    void queueAccWrite(uint8_t laneMask, const float value[4], uint32_t latency);
    void startXgkick(uint32_t qwordAddress);

    FlagPipelineEntry* allocFlagEntry();
    void noteReady(uint64_t readyCycle) {
        if (readyCycle < m_minReady)
            m_minReady = readyCycle;
    }
    void resetScheduler();
    void commitReadyPipelines();
    void advanceOneCycle();
    void advanceTo(uint64_t targetCycle);
    void flushPipelines();
    void progressXgkick();
    void finishXgkick();
    uint64_t calculatePairReadyCycle(const DecodedInstructionPair& decoded) const;
    void markPairWrites(const DecodedInstructionPair& decoded);
    bool pipelinesPending() const;

    static inline float normalizeOperand(float value) {
        // PS2 VU operands: denormals read as (signed) zero, exponent 255 reads as the largest finite value.
        uint32_t bits;
        std::memcpy(&bits, &value, sizeof(bits));
        const uint32_t exponent = (bits >> 23) & 0xFFu;
        if (exponent == 0u)
            bits &= 0x80000000u;
        else if (exponent == 0xFFu)
            bits = (bits & 0x80000000u) | 0x7F7FFFFFu;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    }
    float normalizeResult(float value, uint32_t& laneFlags) const;
    uint32_t microAddressMask() const;
    int32_t readBranchVi(uint8_t reg) const;
    void recordViWriteForBranch(uint8_t reg, int32_t oldValue);
    void reportReservedInstruction(bool upper, uint32_t instruction);
    float broadcast(const float* vf, uint8_t bc);
};

#endif
