#include "motion_history.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <tuple>
#include <utility>
#if defined(_M_X64) || defined(__SSE2__)
#include <emmintrin.h>
#endif

namespace gsr::motion {
bool Key::operator<(const Key& b) const {
    return std::tie(object, generation, mesh, part, pass, batch) <
           std::tie(b.object, b.generation, b.mesh, b.part, b.pass, b.batch);
}
History::~History() {
    delete shadow_;
}
void History::setVerify(bool on) {
    if (on == (shadow_ != nullptr))
        return;
    delete shadow_;
    shadow_ = nullptr;
    if (!on)
        return;
    // From here on both see the same calls; the shadow starts from this history's state as of the next reset.
    shadow_ = new History(budget_, manual_);
    shadow_->fast_ = false;
    reset();
}
void History::recycle(Entries& entries) {
    for (auto& item : entries)
        if (pool_.size() < 4096 && item.second.vertices.capacity())
            pool_.push_back(std::move(item.second.vertices));
    entries.clear();
}
void History::reset() {
    recycle(previous_);
    recycle(current_);
    ambiguous_.clear();
    epoch_ = referenceEpoch_ = bytes_ = 0;
    if (shadow_)
        shadow_->reset();
}
void History::commitFrame() {
    recycle(previous_);
    previous_ = std::move(current_);
    current_.clear();
    referenceEpoch_ = epoch_;
    bytes_ = 0;
    for (const auto& item : previous_)
        bytes_ += item.second.vertices.size() * sizeof(Vertex);
    if (shadow_)
        shadow_->commitFrame();
}
void History::beginFrame(uint64_t epoch, bool cut) {
    if (shadow_)
        shadow_->beginFrame(epoch, cut);
    if (cut || !epoch || (epoch_ && (epoch < epoch_ || (!manual_ && epoch != epoch_ && epoch != epoch_ + 1)))) {
        History* const shadow = std::exchange(shadow_, nullptr); // already done above
        reset();
        shadow_ = shadow;
    }
    if (epoch == epoch_)
        return;
    if (!manual_) {
        History* const shadow = std::exchange(shadow_, nullptr);
        commitFrame();
        shadow_ = shadow;
    }
    recycle(current_);
    ambiguous_.clear();
    epoch_ = epoch;
    bytes_ = 0;
    for (const auto& item : previous_)
        bytes_ += item.second.vertices.size() * sizeof(Vertex);
}
Result History::track(const Key& key, uint64_t topology, const Vertex* vertices, uint32_t count, float sx, float sy,
                      bool known, std::vector<Previous>& output) {
    if (!fast_)
        return trackReference(key, topology, vertices, count, sx, sy, known, output);
    output.clear();
    if (count > 1000000)
        return {};
    output.resize(count);
    return track(key, topology, vertices, count, sx, sy, known, output.data());
}
Result History::track(const Key& key, uint64_t topology, const Vertex* vertices, uint32_t count, float sx, float sy,
                      bool known, Previous* output) {
    Result result;
    if (!fast_) {
        result = trackReference(key, topology, vertices, count, sx, sy, known, shadowOutput_);
        if (shadowOutput_.size() == count)
            std::copy(shadowOutput_.begin(), shadowOutput_.end(), output);
        else
            std::fill(output, output + count, Previous{});
    } else
        result = trackFast(key, topology, vertices, count, sx, sy, known, output);
    if (shadow_)
        compare(result, output, key, topology, vertices, count, sx, sy, known);
    return result;
}
void History::compare(const Result& result, const Previous* output, const Key& key, uint64_t topology,
                      const Vertex* vertices, uint32_t count, float sx, float sy, bool known) {
    std::vector<Previous>& reference = shadowOutput_;
    const Result expected = shadow_->trackReference(key, topology, vertices, count, sx, sy, known, reference);
    bool same = expected.matched == result.matched && expected.vertices == result.vertices && shadow_->bytes_ == bytes_;
    if (same && reference.size() == count)
        same = std::memcmp(reference.data(), output, count * sizeof(Previous)) == 0;
    else if (same) // the first implementation leaves no entries for an oversized batch; this one writes defaults
        same = count > 1000000;
    ++verifyChecked_;
    verifyDiffer_ += !same;
}
// The first implementation: one vertex at a time, leaving each loop at the first vertex that decides.
Result History::trackReference(const Key& key, uint64_t topology, const Vertex* vertices, uint32_t count, float sx,
                               float sy, bool known, std::vector<Previous>& output) {
    output.clear();
    if (count > 1000000)
        return {};
    output.assign(count, {});
    if (!known || !key.object || !key.mesh || !epoch_ || !vertices || !count || count > 1000000 || !std::isfinite(sx) ||
        !std::isfinite(sy) || sx <= 0 || sy <= 0)
        return {};
    // Repeated scope/batch IDs in one world are ambiguous, never a second object.
    if (ambiguous_.count(key))
        return {};
    const auto duplicate = current_.find(key);
    if (duplicate != current_.end()) {
        bytes_ -= duplicate->second.vertices.size() * sizeof(Vertex);
        if (pool_.size() < 4096)
            pool_.push_back(std::move(duplicate->second.vertices));
        current_.erase(duplicate);
        ambiguous_.insert(key);
        return {};
    }
    for (uint32_t i = 0; i < count; ++i) {
        const auto& v = vertices[i];
        if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z) || v.z <= 0 || !std::isfinite(v.u) ||
            !std::isfinite(v.v))
            return {};
    }
    const auto found = previous_.find(key);
    bool match = found != previous_.end() && found->second.epoch == referenceEpoch_ &&
                 found->second.topology == topology && found->second.vertices.size() == count;
    if (match) {
        const Vertex* previous = found->second.vertices.data();
        for (uint32_t i = 0; i < count; ++i) {
            const auto& old = previous[i];
            const auto& now = vertices[i];
            // Texture coordinates identify vertex correspondence, and reject newly
            // generated clipping intersections/reordering instead of inventing flow.
            const float ratio = now.z / old.z;
            if (std::abs(old.u - now.u) > 1e-4f || std::abs(old.v - now.v) > 1e-4f || !std::isfinite(ratio) ||
                ratio < 0.01f || ratio > 100 || std::abs(old.x - now.x) > 4096 || std::abs(old.y - now.y) > 4096) {
                match = false;
                break;
            }
            output[i] = {old.x * sx * ratio, old.y * sy * ratio, ratio, 1};
        }
        if (!match)
            std::fill(output.begin(), output.end(), Previous{});
    }
    store(key, topology, vertices, count);
    return {match, match ? count : 0};
}
void History::store(const Key& key, uint64_t topology, const Vertex* vertices, uint32_t count) {
    const uint64_t size = uint64_t(count) * sizeof(Vertex);
    if (size <= budget_ && bytes_ <= budget_ - size) {
        Entry e;
        e.epoch = epoch_;
        e.topology = topology;
        if (!pool_.empty()) {
            e.vertices = std::move(pool_.back());
            pool_.pop_back();
        }
        e.vertices.assign(vertices, vertices + count);
        current_.emplace(key, std::move(e));
        bytes_ += size;
    }
}
namespace {
// Exponent bits not all set: neither infinite nor NaN (std::isfinite, as integer operations the compiler can
// vectorise).
inline uint32_t finiteBit(float f) {
    uint32_t u;
    std::memcpy(&u, &f, sizeof u);
    return (u & 0x7f800000u) != 0x7f800000u;
}
} // namespace
// The same decisions and entries as trackReference, with every vertex loop free of early exits: a batch is decided
// after the whole loop (the first implementation stopped at the first vertex that decided it; the remaining vertices
// could not change the outcome) and the entries are written straight into the caller's buffer.
Result History::trackFast(const Key& key, uint64_t topology, const Vertex* vertices, uint32_t count, float sx, float sy,
                          bool known, Previous* output) {
    const auto none = [&]() {
        std::fill(output, output + count, Previous{});
        return Result{};
    };
    if (count > 1000000 || !known || !key.object || !key.mesh || !epoch_ || !vertices || !count || !std::isfinite(sx) ||
        !std::isfinite(sy) || sx <= 0 || sy <= 0)
        return none();
    if (ambiguous_.count(key))
        return none();
    const auto duplicate = current_.find(key);
    if (duplicate != current_.end()) {
        bytes_ -= duplicate->second.vertices.size() * sizeof(Vertex);
        if (pool_.size() < 4096)
            pool_.push_back(std::move(duplicate->second.vertices));
        current_.erase(duplicate);
        ambiguous_.insert(key);
        return none();
    }
    uint32_t finite = 1;
    for (uint32_t i = 0; i < count; ++i) {
        const auto& v = vertices[i];
        finite &=
            finiteBit(v.x) & finiteBit(v.y) & finiteBit(v.z) & uint32_t(v.z > 0) & finiteBit(v.u) & finiteBit(v.v);
    }
    if (!finite)
        return none();
    const auto found = previous_.find(key);
    bool match = found != previous_.end() && found->second.epoch == referenceEpoch_ &&
                 found->second.topology == topology && found->second.vertices.size() == count;
    if (match) {
        const Vertex* previous = found->second.vertices.data();
        uint32_t bad = 0;
        for (uint32_t i = 0; i < count; ++i) {
            const auto& old = previous[i];
            const auto& now = vertices[i];
            const float ratio = now.z / old.z;
            bad |= uint32_t(std::abs(old.u - now.u) > 1e-4f) | uint32_t(std::abs(old.v - now.v) > 1e-4f) |
                   (finiteBit(ratio) ^ 1u) | uint32_t(ratio < 0.01f) | uint32_t(ratio > 100) |
                   uint32_t(std::abs(old.x - now.x) > 4096) | uint32_t(std::abs(old.y - now.y) > 4096);
            output[i] = {old.x * sx * ratio, old.y * sy * ratio, ratio, 1};
        }
        match = !bad;
    }
    if (!match)
        std::fill(output, output + count, Previous{});
    store(key, topology, vertices, count);
    return {match, match ? count : 0};
}
void convert(const Source& source, uint32_t count, Vertex* out) {
    for (uint32_t i = 0; i < count; ++i) {
        const float* v = source.data + size_t(i) * source.stride;
        const float q = v[source.q];
        const float iq = source.fixed ? 1.0f : (q != 0 ? 1.0f / q : 0);
        out[i] = {v[source.x], v[source.y], v[source.z], v[source.s] * iq, v[source.t] * iq};
    }
}
Result History::track(const Key& key, uint64_t topology, const Source& source, uint32_t count, float sx, float sy,
                      bool known, Previous* output) {
    const bool convertible = source.data && count <= 1000000;
    if (!fast_) { // the first implementation: convert, then track
        verifyConverted_.resize(convertible ? count : 0);
        if (convertible)
            convert(source, count, verifyConverted_.data());
        return track(key, topology, convertible ? verifyConverted_.data() : nullptr, count, sx, sy, known, output);
    }
    const Result result = trackFused(key, topology, source, count, sx, sy, known, output);
    if (shadow_) {
        // The shadow tracks the vertices converted one at a time; the kept copy is compared directly as well (the
        // next world's entries compare it again through the history).
        verifyConverted_.resize(convertible ? count : 0);
        if (convertible)
            convert(source, count, verifyConverted_.data());
        const uint64_t differBefore = verifyDiffer_;
        compare(result, output, key, topology, convertible ? verifyConverted_.data() : nullptr, count, sx, sy, known);
        if (verifyDiffer_ == differBefore) {
            const auto kept = current_.find(key);
            const auto shadowKept = shadow_->current_.find(key);
            const bool here = kept != current_.end(), there = shadowKept != shadow_->current_.end();
            bool same = here == there;
            if (same && here)
                same = kept->second.vertices.size() == shadowKept->second.vertices.size() &&
                       kept->second.epoch == shadowKept->second.epoch &&
                       kept->second.topology == shadowKept->second.topology &&
                       std::memcmp(kept->second.vertices.data(), shadowKept->second.vertices.data(),
                                   kept->second.vertices.size() * sizeof(Vertex)) == 0;
            verifyDiffer_ += !same;
        }
    }
    return result;
}
namespace {
// One pass of trackFused: convert vertex i into `now` (the copy kept for the next world), gather the finite flags
// and, for a candidate with a previous entry, the correspondence flags and the entry. The expressions are those of
// convert() and trackFast(), so every value is the same bit for bit.
template <bool Fixed, bool Candidate>
void convertTrack(const Source& source, uint32_t count, const Vertex* previous, float sx, float sy, Vertex* now,
                  Previous* output, uint32_t& finiteOut, uint32_t& badOut) {
    uint32_t finite = 1, bad = 0;
    const float* v = source.data;
    const uint32_t stride = source.stride, ox = source.x, oy = source.y, oz = source.z, os = source.s, ot = source.t,
                   oq = source.q;
    uint32_t i = 0;
#if defined(_M_X64) || defined(__SSE2__)
    // Four vertices at a time: the same IEEE operations lane by lane (a division, products, differences, compares
    // that are false for NaN as in the scalar form), the bits of every vertex and entry unchanged; the rest below.
    {
        const __m128 zero = _mm_setzero_ps(), one = _mm_set1_ps(1.0f),
                     signless = _mm_castsi128_ps(_mm_set1_epi32(0x7fffffff));
        const __m128i exponent = _mm_set1_epi32(0x7f800000);
        const __m128 uvLimit = _mm_set1_ps(1e-4f), ratioLow = _mm_set1_ps(0.01f), ratioHigh = _mm_set1_ps(100.0f),
                     jump = _mm_set1_ps(4096.0f), scaleX = _mm_set1_ps(sx), scaleY = _mm_set1_ps(sy);
        // all-ones lanes where a value is infinite or NaN
        const auto notFinite = [&](__m128 f) {
            return _mm_castsi128_ps(_mm_cmpeq_epi32(_mm_and_si128(_mm_castps_si128(f), exponent), exponent));
        };
        __m128 failed = zero, broken = zero; // lanes not finite / not corresponding, accumulated
        for (; i + 4 <= count; i += 4) {
            const float* a = v;
            const float* b = a + stride;
            const float* c = b + stride;
            const float* d = c + stride;
            v = d + stride;
            const __m128 x = _mm_setr_ps(a[ox], b[ox], c[ox], d[ox]), y = _mm_setr_ps(a[oy], b[oy], c[oy], d[oy]),
                         z = _mm_setr_ps(a[oz], b[oz], c[oz], d[oz]);
            __m128 iq = one;
            if constexpr (!Fixed) {
                const __m128 q = _mm_setr_ps(a[oq], b[oq], c[oq], d[oq]);
                iq = _mm_and_ps(_mm_div_ps(one, q), _mm_cmpneq_ps(q, zero)); // q != 0 ? 1 / q : +0
            }
            const __m128 u = _mm_mul_ps(_mm_setr_ps(a[os], b[os], c[os], d[os]), iq),
                         w = _mm_mul_ps(_mm_setr_ps(a[ot], b[ot], c[ot], d[ot]), iq);
            alignas(16) float lx[4], ly[4], lz[4], lu[4], lw[4];
            _mm_store_ps(lx, x);
            _mm_store_ps(ly, y);
            _mm_store_ps(lz, z);
            _mm_store_ps(lu, u);
            _mm_store_ps(lw, w);
            for (unsigned l = 0; l < 4; ++l)
                now[i + l] = {lx[l], ly[l], lz[l], lu[l], lw[l]};
            failed = _mm_or_ps(failed, _mm_or_ps(_mm_or_ps(notFinite(x), notFinite(y)),
                                                 _mm_or_ps(_mm_or_ps(notFinite(z), notFinite(u)), notFinite(w))));
            failed = _mm_or_ps(failed, _mm_andnot_ps(_mm_cmpgt_ps(z, zero), _mm_castsi128_ps(_mm_set1_epi32(-1))));
            if constexpr (Candidate) {
                const Vertex* p = previous + i;
                const __m128 px = _mm_setr_ps(p[0].x, p[1].x, p[2].x, p[3].x),
                             py = _mm_setr_ps(p[0].y, p[1].y, p[2].y, p[3].y),
                             pz = _mm_setr_ps(p[0].z, p[1].z, p[2].z, p[3].z),
                             pu = _mm_setr_ps(p[0].u, p[1].u, p[2].u, p[3].u),
                             pw = _mm_setr_ps(p[0].v, p[1].v, p[2].v, p[3].v);
                const __m128 ratio = _mm_div_ps(z, pz);
                const auto apart = [&](__m128 before, __m128 after, __m128 limit) {
                    return _mm_cmpgt_ps(_mm_and_ps(_mm_sub_ps(before, after), signless), limit);
                };
                broken = _mm_or_ps(broken, _mm_or_ps(_mm_or_ps(apart(pu, u, uvLimit), apart(pw, w, uvLimit)),
                                                     _mm_or_ps(apart(px, x, jump), apart(py, y, jump))));
                broken = _mm_or_ps(broken, _mm_or_ps(notFinite(ratio), _mm_or_ps(_mm_cmplt_ps(ratio, ratioLow),
                                                                                 _mm_cmpgt_ps(ratio, ratioHigh))));
                __m128 ex = _mm_mul_ps(_mm_mul_ps(px, scaleX), ratio), ey = _mm_mul_ps(_mm_mul_ps(py, scaleY), ratio),
                       er = ratio, ev = one;
                _MM_TRANSPOSE4_PS(ex, ey, er, ev);
                _mm_storeu_ps(&output[i].x, ex);
                _mm_storeu_ps(&output[i + 1].x, ey);
                _mm_storeu_ps(&output[i + 2].x, er);
                _mm_storeu_ps(&output[i + 3].x, ev);
            }
        }
        finite = _mm_movemask_ps(failed) ? 0u : 1u;
        bad = _mm_movemask_ps(broken) ? 1u : 0u;
    }
#endif
    for (; i < count; ++i, v += stride) {
        float iq = 1.0f;
        if constexpr (!Fixed) {
            const float q = v[oq];
            iq = q != 0 ? 1.0f / q : 0;
        }
        const Vertex c{v[ox], v[oy], v[oz], v[os] * iq, v[ot] * iq};
        now[i] = c;
        finite &=
            finiteBit(c.x) & finiteBit(c.y) & finiteBit(c.z) & uint32_t(c.z > 0) & finiteBit(c.u) & finiteBit(c.v);
        if constexpr (Candidate) {
            const Vertex& old = previous[i];
            const float ratio = c.z / old.z;
            bad |= uint32_t(std::abs(old.u - c.u) > 1e-4f) | uint32_t(std::abs(old.v - c.v) > 1e-4f) |
                   (finiteBit(ratio) ^ 1u) | uint32_t(ratio < 0.01f) | uint32_t(ratio > 100) |
                   uint32_t(std::abs(old.x - c.x) > 4096) | uint32_t(std::abs(old.y - c.y) > 4096);
            output[i] = {old.x * sx * ratio, old.y * sy * ratio, ratio, 1};
        }
    }
    finiteOut = finite;
    badOut = bad;
}
} // namespace
// trackFast's decisions in one pass. The lookups that do not depend on the vertices come first (the first
// implementation made them before reading a vertex too, apart from the previous entry, which nothing in between
// changes), then one loop converts, checks and writes the entries, and the batch is decided after it. A batch that is
// not finite stores nothing and has default entries throughout, as before.
Result History::trackFused(const Key& key, uint64_t topology, const Source& source, uint32_t count, float sx, float sy,
                           bool known, Previous* output) {
    const auto none = [&]() {
        std::fill(output, output + count, Previous{});
        return Result{};
    };
    if (count > 1000000 || !known || !key.object || !key.mesh || !epoch_ || !source.data || !count ||
        !std::isfinite(sx) || !std::isfinite(sy) || sx <= 0 || sy <= 0)
        return none();
    if (ambiguous_.count(key))
        return none();
    const auto duplicate = current_.find(key);
    if (duplicate != current_.end()) {
        bytes_ -= duplicate->second.vertices.size() * sizeof(Vertex);
        if (pool_.size() < 4096)
            pool_.push_back(std::move(duplicate->second.vertices));
        current_.erase(duplicate);
        ambiguous_.insert(key);
        return none();
    }
    const auto found = previous_.find(key);
    const bool candidate = found != previous_.end() && found->second.epoch == referenceEpoch_ &&
                           found->second.topology == topology && found->second.vertices.size() == count;
    if (staged_.size() < count)
        staged_.resize(count);
    const Vertex* previous = candidate ? found->second.vertices.data() : nullptr;
    Vertex* const now = staged_.data();
    uint32_t finite = 0, bad = 0;
    if (source.fixed) {
        if (candidate)
            convertTrack<true, true>(source, count, previous, sx, sy, now, output, finite, bad);
        else
            convertTrack<true, false>(source, count, previous, sx, sy, now, output, finite, bad);
    } else if (candidate)
        convertTrack<false, true>(source, count, previous, sx, sy, now, output, finite, bad);
    else
        convertTrack<false, false>(source, count, previous, sx, sy, now, output, finite, bad);
    if (!finite)
        return none();
    const bool match = candidate && !bad;
    if (!match)
        std::fill(output, output + count, Previous{});
    storeStaged(key, topology, count);
    return {match, match ? count : 0};
}
void History::storeStaged(const Key& key, uint64_t topology, uint32_t count) {
    const uint64_t size = uint64_t(count) * sizeof(Vertex);
    if (size <= budget_ && bytes_ <= budget_ - size) {
        Entry e;
        e.epoch = epoch_;
        e.topology = topology;
        e.vertices = std::move(staged_);
        e.vertices.resize(count); // the staged vector may be longer from an earlier batch
        current_.emplace(key, std::move(e));
        bytes_ += size;
        staged_.clear();
        if (!pool_.empty()) {
            staged_ = std::move(pool_.back());
            pool_.pop_back();
        }
    }
}
} // namespace gsr::motion
