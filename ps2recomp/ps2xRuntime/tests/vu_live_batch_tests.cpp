#include "ps2_vu1_batch.h"
#include "drakan_vu1_hook.h"
#include "runtime/ps2_memory.h"
#include "runtime/ps2_vu1.h"
#include "runtime/ps2_render_queue.h"
#include "runtime/gs/gs_frontend.h"
#include "runtime/object_motion_tracker.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <tuple>
#include <vector>

namespace {
using Camera = std::tuple<uint32_t, uint32_t, uint64_t>;
std::vector<Camera> cameras;
std::vector<std::pair<uint32_t, uint64_t>> observations;
void camera(uint32_t first, uint32_t count, const uint8_t* data) {
    uint64_t hash = 1469598103934665603ull;
    for (size_t i = 0; i < 16384; ++i) {
        hash ^= data[i];
        hash *= 1099511628211ull;
    }
    cameras.emplace_back(first, count, hash);
    observations.emplace_back(4, hash);
}
struct Packet {
    uint8_t path;
    std::vector<uint8_t> bytes;
    bool operator==(const Packet&) const = default;
};
struct Fixture {
    PS2Memory memory;
    VU1Interpreter vu;
    GS gs;
    GifArbiter gif;
    std::vector<Packet> packets;
    const vu1async::Meta* inlineMeta = nullptr;
    uint64_t calls = 0, published = 0;
    Fixture(const std::vector<uint8_t>& code, const VU1State& initial) {
        if (!memory.initialize())
            std::abort();
        std::memcpy(memory.getVU1Code(), code.data(), code.size());
        memory.markVU1CodeModified();
        vu.state() = initial;
        gif.setProcessPacketFn([](const uint8_t*, uint32_t) {});
        gif.setProcessPathPacketFn([this](uint8_t path, const uint8_t* data, uint32_t size) {
            packets.push_back({path, {data, data + size}});
            uint64_t hash = 0;
            for (uint32_t i = 0; i < size; ++i)
                hash = hash * 131u + data[i];
            observations.emplace_back(path, hash);
        });
        memory.setGifArbiter(&gif);
        memory.setVu1MscalCallback([this](uint32_t pc, uint32_t top, uint32_t itop) { execute(false, pc, top, itop); });
        memory.setVu1MscntCallback([this](uint32_t top, uint32_t itop) { execute(true, 0, top, itop); });
    }
    void execute(bool resume, uint32_t pc, uint32_t top, uint32_t itop) {
        const auto* meta = vu1async::currentMeta();
        if (!meta)
            meta = inlineMeta;
        vu.state().dBitEnabled = meta->dEn;
        vu.state().tBitEnabled = meta->tEn;
        if (resume)
            vu.resume(memory.getVU1Code(), 16384, memory.getVU1Data(), 16384, gs, &memory, top, itop);
        else
            vu.execute(memory.getVU1Code(), 16384, memory.getVU1Data(), 16384, gs, &memory, pc, top, itop);
        ++calls;
    }
    void serial(const vu1batch::Input& in) {
        if (in.task) {
            in.task();
            return;
        }
        if (in.path3) {
            memory.submitGifPacket(GifPathId::Path3, in.data.data(), uint32_t(in.data.size()), true);
            return;
        }
        inlineMeta = &in.meta;
        memory.runVif1Job(in.data.data(), uint32_t(in.data.size()), in.meta.objectMotion.get());
        inlineMeta = nullptr;
    }
    void bind() {
        vu1batch::bind(memory, vu, [this](uint64_t, uint64_t runs, uint64_t) { published += runs; });
    }
};
void word(std::vector<uint8_t>& out, uint32_t value) {
    const auto* p = reinterpret_cast<const uint8_t*>(&value);
    out.insert(out.end(), p, p + 4);
}
bool same(const VU1State& a, const VU1State& b) {
#define CMP(x)                                                                                                         \
    if (std::memcmp(&a.x, &b.x, sizeof a.x))                                                                           \
        return false;
    // clang-format off
    CMP(vf)
    CMP(vi)
    CMP(acc)
    CMP(q)
    CMP(p)
    CMP(i)
    CMP(r)
    CMP(pc)
    CMP(mac)
    CMP(clip)
    CMP(status)
    CMP(cycles)
    CMP(ebit)
    CMP(haltAfterDelaySlot)
    CMP(dBitEnabled)
    CMP(tBitEnabled)
    CMP(stoppedByD)
    CMP(stoppedByT)
    CMP(top)
    CMP(itop)
    CMP(branchPending)
    CMP(branchTarget)
    CMP(branchDelay)
    // clang-format on
#undef CMP
    return true;
}
} // namespace
bool positiveGateCases(const std::vector<uint8_t>& code) {
    VU1State initial{};
    initial.vf[0][3] = 1.0f;
    auto serial = std::make_unique<Fixture>(code, initial), worker = std::make_unique<Fixture>(code, initial);
    const char* pred = std::getenv("DRAKAN_VU_BATCH_PREDICT");
    const bool predicted = !(pred && pred[0] == '0');
    const char* gates = std::getenv("DRAKAN_VU_BATCH_PREDICATES");
    const bool relaxed = gates && gates[0] && gates[0] != '0';
    const char* verify = std::getenv("DRAKAN_VU_BATCH_VERIFY");
    const bool checking = verify && verify[0] && verify[0] != '0';
    const std::array<std::pair<uint32_t, int16_t>, 8> cases = {
        {{0, 1}, {0, 2}, {1, 2}, {1, 0}, {2, -1}, {2, -2}, {3, 0}, {3, 4}}};
    bool ok = true;
    for (unsigned index = 0; index < cases.size(); ++index) {
        const auto [identity, count] = cases[index];
        for (auto* f : {serial.get(), worker.get()}) {
            f->vu.reset();
            f->memory.vif1_regs = {};
            std::memset(f->memory.getVU1Data(), 0, 16384);
            const uint32_t value = uint16_t(count), endTag = 0x8000u;
            std::memcpy(f->memory.getVU1Data() + 65 * 16, &value, 4);
            std::memcpy(f->memory.getVU1Data() + 11 * 16, &endTag, 4);
            std::memcpy(f->memory.getVU1Data() + 7 * 16, &identity, 4);
            f->packets.clear();
            f->calls = f->published = 0;
        }
        observations.clear();
        std::vector<vu1batch::Input> inputs(2);
        for (auto& in : inputs)
            in.memory = &worker->memory;
        word(inputs[0].data, 0x15000000u | 60u);
        word(inputs[1].data, 0x14000000u | (0x1408u / 8u));
        for (const auto& in : inputs)
            serial->serial(in);
        const auto expected = observations;
        observations.clear();
        worker->bind();
        const bool handled = vu1batch::process(inputs, [&](const auto& in) { worker->serial(in); });
        const bool equal = same(serial->vu.state(), worker->vu.state()) &&
                           !std::memcmp(serial->memory.getVU1Data(), worker->memory.getVU1Data(), 16384) &&
                           serial->packets == worker->packets && expected == observations &&
                           ps2GetScratchpadHostPtr() == worker->memory.m_scratchpad && vu1batch::healthy();
        bool scheduled = true;
        if (predicted && !checking && (index & 1u))
            scheduled =
                index == 1 ? (relaxed ? worker->published == 1 : worker->published == 0) : worker->published == 0;
        if (!handled || !equal || !scheduled)
            std::fprintf(stderr, "FAIL clipping gate case=%u count=%d handled=%u equal=%u collected=%llu\n", index,
                         int(count), unsigned(handled), unsigned(equal), (unsigned long long)worker->published);
        ok &= handled && equal && scheduled;
    }
    vu1batch::unbind(worker->memory);
    observations.clear();
    return ok;
}
bool lazyLoadCases(const std::vector<uint8_t>& code) {
    VU1State initial{};
    initial.vf[0][3] = 1.0f;
    auto serial = std::make_unique<Fixture>(code, initial), worker = std::make_unique<Fixture>(code, initial);
    const char* pred = std::getenv("DRAKAN_VU_BATCH_PREDICT");
    const bool predicted = !(pred && pred[0] == '0');
    const char* loads = std::getenv("DRAKAN_VU_BATCH_LAZY_LOADS");
    const bool deferred = loads && loads[0] && loads[0] != '0';
    const char* verify = std::getenv("DRAKAN_VU_BATCH_VERIFY");
    const bool checking = verify && verify[0] && verify[0] != '0';
    bool ok = true;
    for (unsigned consumed = 0; consumed < 4; ++consumed)
        for (unsigned pass = 0; pass < 2; ++pass) {
            for (auto* f : {serial.get(), worker.get()}) {
                f->vu.reset();
                f->memory.vif1_regs = {};
                std::memset(f->memory.getVU1Data(), 0, 16384);
                f->packets.clear();
                f->calls = f->published = 0;
                const uint32_t values[4] = {pass ? 0x40000001u : 0x3f000001u, 0x80000000u, 0x7fc12345u, 0xff800000u};
                const uint32_t identity = 0x44454144u + consumed * 2u, endTag = 0x8000u, start = 65u;
                std::memcpy(f->memory.getVU1Data() + 65 * 16, values, 16);
                std::memcpy(f->memory.getVU1Data() + 11 * 16, &endTag, 4);
                std::memcpy(f->memory.getVU1Data() + 7 * 16, &identity, 4);
                if (consumed == 1)
                    std::memcpy(f->memory.getVU1Data() + 9 * 16, &start, 4);
                if (consumed == 3) {
                    const uint32_t one = 1;
                    std::memcpy(f->memory.getVU1Data() + 9 * 16 + 8, &one, 4);
                }
            }
            observations.clear();
            std::vector<vu1batch::Input> inputs(2);
            for (auto& in : inputs)
                in.memory = &worker->memory;
            word(inputs[0].data, 0x15000000u | 60u);
            word(inputs[1].data, 0x14000000u | (consumed == 1 ? 0x1408u / 8u : 489u));
            if (consumed >= 2)
                word(inputs[1].data, 0x17000000u);
            for (const auto& in : inputs)
                serial->serial(in);
            const auto expected = observations;
            observations.clear();
            worker->bind();
            const bool handled = vu1batch::process(inputs, [&](const auto& in) { worker->serial(in); });
            const bool equal = same(serial->vu.state(), worker->vu.state()) &&
                               !std::memcmp(serial->memory.getVU1Data(), worker->memory.getVU1Data(), 16384) &&
                               serial->packets == worker->packets && expected == observations &&
                               ps2GetScratchpadHostPtr() == worker->memory.m_scratchpad && vu1batch::healthy();
            const bool scheduled =
                !predicted || checking || !pass || (consumed == 2 && loads && loads[0] == '1') ||
                (worker->published == (deferred && (consumed == 0 || consumed == 3) ? (consumed == 3 ? 2u : 1u) : 0u));
            if (!handled || !equal || !scheduled)
                std::fprintf(stderr, "FAIL deferred load consumed=%u pass=%u handled=%u equal=%u collected=%llu\n",
                             consumed, pass, unsigned(handled), unsigned(equal), (unsigned long long)worker->published);
            if (!scheduled)
                vu1batch::printStats();
            ok &= handled && equal && scheduled;
        }
    vu1batch::unbind(worker->memory);
    observations.clear();
    return ok;
}
int main(int argc, char** argv) {
    if (argc < 2 || argc > 4)
        return 2;
    bool sparse = false, joined = false, repeat = false;
    for (int i = 2; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--sparse"))
            sparse = true;
        else if (!std::strcmp(argv[i], "--joined"))
            joined = true;
        else if (!std::strcmp(argv[i], "--repeat"))
            repeat = true;
        else
            return 2;
    }
    if (sparse && joined)
        return 2;
    FILE* f = std::fopen(argv[1], "rb");
    std::array<uint32_t, 4> h{};
    if (!f || std::fread(h.data(), sizeof h, 1, f) != 1 || h[0] != 0x31525556 || h[2] != 16384 ||
        h[3] != sizeof(VU1State) || h[1] > 10000)
        return 2;
    std::vector<uint8_t> code(h[2]);
    VU1State initial{};
    if (std::fread(code.data(), 1, code.size(), f) != code.size() || std::fread(&initial, sizeof initial, 1, f) != 1)
        return 2;
    std::vector<vu1batch::Input> jobs(h[1]);
    for (size_t i = 0; i < jobs.size(); ++i) {
        uint32_t e[6];
        std::array<uint8_t, 16384> data;
        if (std::fread(e, sizeof e, 1, f) != 1 || std::fread(data.data(), 1, data.size(), f) != data.size())
            return 2;
        auto& in = jobs[i];
        in.meta.dEn = e[4] != 0;
        in.meta.tEn = e[5] != 0;
        if (i % 4 == 0)
            word(in.data, 0x06000000);
        word(in.data, 0x01000101);
        word(in.data, 0x05000000);
        for (unsigned q = 0; q < 4; ++q) {
            word(in.data, 0x6c000000 | (q * 256));
            in.data.insert(in.data.end(), data.begin() + q * 4096, data.begin() + (q + 1) * 4096);
        }
        word(in.data, 0x03000000 | e[2]);
        word(in.data, 0x02000000);
        word(in.data, 0x04000000 | e[3]);
        word(in.data, e[0] ? 0x17000000 : 0x14000000 | (e[1] / 8));
        if (i % 4 == 2)
            word(in.data, 0x06008000);
        // Exercise data/row/column/preserve masks after geometry. The following
        // captured call uploads its original input, so guest geometry is intact.
        if (i % 11 == 0) {
            word(in.data, 0x30000000);
            for (unsigned c = 0; c < 4; ++c)
                word(in.data, 0x12340000u + c);
            word(in.data, 0x31000000);
            for (unsigned c = 0; c < 4; ++c)
                word(in.data, 0x56780000u + c);
            word(in.data, 0x20000000);
            word(in.data, 0xe4e4e4e4);
            word(in.data, 0x7c0103e8);
            for (unsigned c = 0; c < 4; ++c)
                word(in.data, 0x9abc0000u + c);
        }
        if (i % 16 == 0) {
            word(in.data, 0x50000001);
            word(in.data, 0x8000);
            word(in.data, 0);
            word(in.data, 0);
            word(in.data, 0);
        }
        if (i % 3 == 0) {
            auto motion = std::make_shared<drakan_object_motion::Job>();
            motion->byteCount = uint32_t(in.data.size());
            gsr::ObjectMotionScope s{};
            s.objectId = i + 1;
            s.flags = gsr::ObjectMotionScope::Valid;
            motion->markers.push_back({0, s});
            in.meta.objectMotion = motion;
        }
    }
    std::fclose(f);
    const size_t geometryCalls = jobs.size();
    if (joined) {
        std::vector<vu1batch::Input> packed;
        for (size_t i = 0; i < jobs.size();) {
            vu1batch::Input in;
            in.meta = jobs[i].meta;
            auto motion = std::make_shared<drakan_object_motion::Job>();
            for (unsigned n = 0; n < 3 && i < jobs.size(); ++n, ++i) {
                if (jobs[i].meta.dEn != in.meta.dEn || jobs[i].meta.tEn != in.meta.tEn)
                    std::abort();
                if (jobs[i].meta.objectMotion)
                    for (auto m : jobs[i].meta.objectMotion->markers) {
                        m.byteOffset += uint32_t(in.data.size());
                        motion->markers.push_back(m);
                    }
                in.data.insert(in.data.end(), jobs[i].data.begin(), jobs[i].data.end());
            }
            motion->byteCount = uint32_t(in.data.size());
            in.meta.objectMotion = motion->markers.empty() ? nullptr : motion;
            packed.push_back(std::move(in));
        }
        jobs = std::move(packed);
    }
    std::vector<vu1batch::Input> mixed;
    mixed.reserve(jobs.size() * 3);
    for (size_t i = 0; i < jobs.size(); ++i) {
        mixed.push_back(std::move(jobs[i]));
        vu1batch::Input p;
        p.path3 = true;
        word(p.data, 0x8000);
        word(p.data, 0);
        word(p.data, 0);
        word(p.data, 0);
        mixed.push_back(std::move(p));
        vu1batch::Input marker;
        marker.task = [i] { observations.emplace_back(5, i); };
        mixed.push_back(std::move(marker));
    }
    jobs = std::move(mixed);
    if (!positiveGateCases(code) || !lazyLoadCases(code))
        return 1;
    auto baseline = std::make_unique<Fixture>(code, initial), candidate = std::make_unique<Fixture>(code, initial);
    // Creating isolated VIF executors must never clear or replace the EE's
    // process-wide scratchpad binding, even while jobs execute on other cores.
    auto* scratch = candidate->memory.m_scratchpad;
    drakan_vu1::g_uploadHook.store(&camera);
    for (auto& in : jobs) {
        in.memory = &baseline->memory;
        if (sparse && in.vif()) {
            uint32_t first;
            std::memcpy(&first, in.data.data(), 4);
            const size_t prefix = 8 + (first == 0x06000000u ? 4 : 0);
            std::array<uint8_t, 16384> input;
            for (unsigned q = 0; q < 4; ++q)
                std::memcpy(input.data() + q * 4096, in.data.data() + prefix + q * 4100 + 4, 4096);
            std::vector<uint8_t> compact(in.data.begin(), in.data.begin() + prefix);
            // Use the serial oracle only to author this test's immutable VIF
            // inputs. The production preparer never executes guest geometry.
            for (unsigned q = 0; q < 1024;) {
                if (!std::memcmp(input.data() + q * 16, baseline->memory.getVU1Data() + q * 16, 16)) {
                    ++q;
                    continue;
                }
                const unsigned begin = q++;
                while (q < 1024 && q - begin < 256 &&
                       std::memcmp(input.data() + q * 16, baseline->memory.getVU1Data() + q * 16, 16))
                    ++q;
                word(compact, 0x6c000000u | ((q - begin) & 255u) << 16u | begin);
                compact.insert(compact.end(), input.begin() + begin * 16, input.begin() + q * 16);
            }
            compact.insert(compact.end(), in.data.begin() + prefix + 4 * 4100, in.data.end());
            in.data = std::move(compact);
        }
        baseline->serial(in);
    }
    const auto expectedCamera = cameras;
    const auto expectedOrder = observations;
    cameras.clear();
    observations.clear();
    candidate->bind();
    for (auto& in : jobs) {
        in.memory = &candidate->memory;
        if (in.task)
            vu1async::submitRenderTask(in.task);
        else if (in.path3)
            vu1async::submitPath3(&candidate->memory, std::vector<uint8_t>(in.data));
        else
            vu1async::submit(&candidate->memory, std::vector<uint8_t>(in.data), in.meta);
    }
    vu1async::sync();
    auto check = [&](const auto& expectedCamera, const auto& expectedOrder) {
        return same(baseline->vu.state(), candidate->vu.state()) &&
               !std::memcmp(baseline->memory.getVU1Data(), candidate->memory.getVU1Data(), 16384) &&
               !std::memcmp(&baseline->memory.vif1_regs, &candidate->memory.vif1_regs, sizeof(VIFRegisters)) &&
               baseline->memory.m_path3Masked == candidate->memory.m_path3Masked &&
               baseline->memory.m_path3MaskedFifo == candidate->memory.m_path3MaskedFifo &&
               baseline->packets == candidate->packets && expectedCamera == cameras && expectedOrder == observations &&
               ps2GetScratchpadHostPtr() == scratch && candidate->calls + candidate->published == geometryCalls &&
               vu1async::g_pending.load() == 0 && vu1batch::healthy();
    };
    bool ok = check(expectedCamera, expectedOrder);
    if (repeat) {
        cameras.clear();
        observations.clear();
        baseline->packets.clear();
        candidate->packets.clear();
        baseline->calls = candidate->calls = candidate->published = 0;
        for (auto& in : jobs) {
            if (in.meta.objectMotion) {
                auto motion = std::make_shared<drakan_object_motion::Job>(*in.meta.objectMotion);
                for (auto& m : motion->markers) {
                    m.scope.frameEpoch += 1000;
                    m.scope.sceneSerial += 1000;
                }
                in.meta.objectMotion = motion;
            }
            in.memory = &baseline->memory;
            baseline->serial(in);
        }
        const auto nextCamera = cameras;
        const auto nextOrder = observations;
        cameras.clear();
        observations.clear();
        for (auto& in : jobs) {
            in.memory = &candidate->memory;
            if (in.task)
                vu1async::submitRenderTask(in.task);
            else if (in.path3)
                vu1async::submitPath3(&candidate->memory, std::vector<uint8_t>(in.data));
            else
                vu1async::submit(&candidate->memory, std::vector<uint8_t>(in.data), in.meta);
        }
        vu1async::sync();
        ok &= check(nextCamera, nextOrder);
    }
    // Unsupported VIF commands are rejected before changing any owner state.
    for (uint32_t command : {0x4a010000u, 0x14000000u}) {
        std::vector<vu1batch::Input> barrier(2);
        for (auto& b : barrier)
            b.memory = &candidate->memory;
        word(barrier[0].data, command);
        if (command >> 24 == 0x4a)
            for (unsigned c = 0; c < 2; ++c)
                word(barrier[0].data, 0);
        if (command >> 24 == 0x14)
            for (unsigned c = 0; c < 512; ++c)
                word(barrier[0].data, command);
        word(barrier[1].data, 0);
        const auto state = candidate->vu.state();
        const auto vif = candidate->memory.vif1_regs;
        std::array<uint8_t, 16384> data, codeCopy;
        std::memcpy(data.data(), candidate->memory.getVU1Data(), 16384);
        std::memcpy(codeCopy.data(), candidate->memory.getVU1Code(), 16384);
        bool called = false;
        ok &= !vu1batch::process(barrier, [&](const auto&) { called = true; }) && !called &&
              same(state, candidate->vu.state()) && !std::memcmp(&vif, &candidate->memory.vif1_regs, sizeof vif) &&
              !std::memcmp(data.data(), candidate->memory.getVU1Data(), 16384) &&
              !std::memcmp(codeCopy.data(), candidate->memory.getVU1Code(), 16384);
    }
    // Closed groups may start before a later command rejects the batch.
    // Exercise those exits (and an owner exception) with inert ending pairs,
    // then release every input immediately after process returns/throws.
    uint32_t ending = 0;
    for (uint32_t pc = 1; pc + 1 < 16384 / 8; ++pc) {
        uint32_t words[4];
        std::memcpy(words, candidate->memory.getVU1Code() + pc * 8, sizeof words);
        if (words[1] == 0x400002ffu && (words[0] == 0 || words[0] == 0x8000033cu) && words[3] == 0x2ffu &&
            (words[2] == 0 || words[2] == 0x8000033cu)) {
            ending = pc;
            break;
        }
    }
    ok &= ending != 0;
    for (unsigned failure = 0; failure < 3; ++failure) {
        std::vector<vu1batch::Input> late(17);
        for (auto& in : late) {
            in.memory = &candidate->memory;
            word(in.data, 0x15000000u | ending);
        }
        if (failure < 2) {
            vu1batch::Input tail;
            tail.memory = &candidate->memory;
            if (failure == 0) {
                word(tail.data, 0x4a010000u);
                word(tail.data, 0);
                word(tail.data, 0);
            } else
                for (unsigned i = 0; i < 505; ++i)
                    word(tail.data, 0x15000000u | ending);
            late.push_back(std::move(tail));
        }
        const auto state = candidate->vu.state();
        const auto vif = candidate->memory.vif1_regs;
        const auto packets = candidate->packets.size(), cameraCount = cameras.size(), records = observations.size();
        std::array<uint8_t, 16384> data, codeCopy;
        std::memcpy(data.data(), candidate->memory.getVU1Data(), 16384);
        std::memcpy(codeCopy.data(), candidate->memory.getVU1Code(), 16384);
        bool called = false, threw = false;
        struct OwnerFailure {};
        try {
            const bool handled = vu1batch::process(late, [&](const auto&) {
                called = true;
                if (failure == 2)
                    throw OwnerFailure{};
            });
            ok &= !handled && failure != 2;
        } catch (const OwnerFailure&) {
            threw = true;
        }
        ok &= (failure == 2 ? called && threw : !called && !threw) && same(state, candidate->vu.state()) &&
              !std::memcmp(&vif, &candidate->memory.vif1_regs, sizeof vif) &&
              !std::memcmp(data.data(), candidate->memory.getVU1Data(), 16384) &&
              !std::memcmp(codeCopy.data(), candidate->memory.getVU1Code(), 16384) &&
              packets == candidate->packets.size() && cameraCount == cameras.size() && records == observations.size() &&
              ps2GetScratchpadHostPtr() == scratch;
    }
    vu1async::printStats();
    vu1async::shutdown();
    vu1batch::unbind(candidate->memory);
    vu1batch::shutdown();
    drakan_vu1::g_uploadHook.store(nullptr);
    std::printf("%s: %zu live VIF calls, %zu ordered packets, %zu camera updates; %llu serial / %llu collected runs; "
                "mixed markers, masked uploads, code/mask barriers, EE scratchpad and completion preserved\n",
                ok ? "PASS" : "FAIL", geometryCalls, candidate->packets.size(), cameras.size(),
                (unsigned long long)candidate->calls, (unsigned long long)candidate->published);
    return ok ? 0 : 1;
}
