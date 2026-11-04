#include "sas/rack.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace sas {

namespace {
constexpr const char* kNames[kModuleTypeCount] = {"Amp", "Cabinet", "Gate", "Compressor", "Overdrive", "NamPedal", "Phaser", "Chorus", "Vibe", "Pitch", "Eq", "Delay", "Reverb", "Splitter", "Mixer"};
}

const char* moduleTypeName(ModuleType t) { return kNames[std::size_t(t)]; }

std::optional<ModuleType> moduleTypeFromName(std::string_view name) {
    for (int i = 0; i < kModuleTypeCount; ++i)
        if (name == kNames[i]) return static_cast<ModuleType>(i);
    return std::nullopt;
}

Channels moduleChannels(ModuleType type) {
    switch (type) {
        case ModuleType::Cabinet: return Channels::MonoToStereo;
        case ModuleType::Phaser:
        case ModuleType::Chorus:
        case ModuleType::Vibe: return Channels::Flexible;
        case ModuleType::Eq:
        case ModuleType::Delay:
        case ModuleType::Reverb: return Channels::Stereo;
        default: return Channels::MonoOnly;
    }
}

Rack::Rack() {
    for (auto& row : table_)
        for (auto& c : row) c.store(nullptr);
    current_.store(&orders_[0]);
}

Rack::~Rack() = default;

void Rack::prepare(double fs, int maxBlock) {
    fs_ = fs;
    maxBlock_ = maxBlock;
    fadeLen_ = std::max(1, int(0.008 * fs));
    laneBl_.assign(std::size_t(std::max(1, maxBlock)), 0.0f);
    laneBr_.assign(std::size_t(std::max(1, maxBlock)), 0.0f);
    for (auto& ring : ring_) ring.assign(kDelay, 0.0f);
    lanesInit_ = false;
    for (auto& row : owned_)
        for (auto& m : row)
            if (m) m->prepare(fs, maxBlock);
}

void Rack::retire(SlotRef slot, std::unique_ptr<Module> module, std::uint64_t needGen) {
    // `blocks_ + 1`: a block that is running now has ended, or one has run since.
    retired_.push_back({std::move(module), slot, needGen, blocks_.load() + 1});
}

void Rack::collect() {
    const auto applied = appliedGen_.load(), blocks = blocks_.load();
    for (auto it = retired_.begin(); it != retired_.end();) {
        if (applied >= it->needGen && blocks >= it->needBlocks) {
            Module* expected = it->module.get();
            cell(it->slot).compare_exchange_strong(expected, nullptr);  // unless a newer module took the slot
            it = retired_.erase(it);
        } else {
            ++it;
        }
    }
}

void Rack::setModule(SlotRef slot, std::unique_ptr<Module> module) {
    if (!valid(slot)) return;
    collect();
    if (module) module->prepare(fs_, maxBlock_);
    auto& owned = owned_[std::size_t(slot.type)][std::size_t(slot.instance)];
    auto old = std::move(owned);
    owned = std::move(module);
    cell(slot).store(owned.get());  // the audio thread takes the new one at its next lookup
    if (old) retire(slot, std::move(old), 0);
}

void Rack::removeModule(SlotRef slot) {
    if (!valid(slot)) return;
    collect();
    auto old = std::move(owned_[std::size_t(slot.type)][std::size_t(slot.instance)]);
    // The cell keeps it: an order that is fading out may still run it. It is freed once the order without it is applied (the one published last).
    if (old) retire(slot, std::move(old), gen_);
}

Module* Rack::module(SlotRef slot) const { return valid(slot) ? owned_[std::size_t(slot.type)][std::size_t(slot.instance)].get() : nullptr; }

void Rack::publish(const RackOrder& order, Transition how) {
    nextOrder_ = (nextOrder_ + 1) % orders_.size();
    orders_[nextOrder_] = order;
    orders_[nextOrder_].gen = ++gen_;
    duck_.store(how == Transition::Duck);
    current_.store(&orders_[nextOrder_]);
}

RackOrder Rack::order() const { return *current_.load(); }

bool Rack::process(float* l, float* r, std::size_t n) {
    const RackOrder* wanted = current_.load();
    if (!applied_) applied_ = wanted;
    if (wanted != applied_ && fade_ == Fade::Idle && !duck_.load()) applied_ = wanted;
    // Chunks: the block is cut where a fade-out ends, because the order changes there.
    struct Piece { std::size_t from, len; bool stereo; };
    Piece pieces[5];
    int count = 0;
    bool anyStereo = false, anyMono = false;
    std::size_t done = 0;
    while (done < n) {
        if (fade_ == Fade::Idle && wanted != applied_) {
            fade_ = Fade::Out;
            fadeLeft_ = fadeLen_;
        }
        std::size_t len = n - done;
        if (fade_ != Fade::Idle) len = std::min<std::size_t>(len, std::size_t(fadeLeft_));
        const bool stereo = runOrder(applied_, l + done, r + done, len);
        if (fade_ != Fade::Idle) {
            for (std::size_t i = 0; i < len; ++i) {
                const float t = float(fadeLeft_ - int(i) - 1) / float(fadeLen_);  // 1 -> 0 over a fade
                const float g = fade_ == Fade::Out ? t : 1.0f - t;
                l[done + i] *= g;
                if (stereo) r[done + i] *= g;
            }
            fadeLeft_ -= int(len);
            if (fadeLeft_ <= 0) {
                if (fade_ == Fade::Out) {
                    applied_ = wanted;  // silence: the new order starts here
                    fade_ = Fade::In;
                    fadeLeft_ = fadeLen_;
                } else {
                    fade_ = Fade::Idle;
                }
            }
        }
        if (count < 5) pieces[count++] = {done, len, stereo};
        (stereo ? anyStereo : anyMono) = true;
        done += len;
    }
    if (anyStereo && anyMono)
        for (int p = 0; p < count; ++p)
            if (!pieces[p].stereo) std::copy(l + pieces[p].from, l + pieces[p].from + pieces[p].len, r + pieces[p].from);
    // The order that is in force now is the one that was asked for and no fade is under way: older ones, and what they held, are done with.
    if (fade_ == Fade::Idle && applied_ == wanted) appliedGen_.store(wanted->gen);
    blocks_.fetch_add(1);
    return anyStereo;
}

// The entries of order[from, to) that belong to `lane` (-1: all of them), in order.
Rack::Segment Rack::segmentOf(const RackOrder& order, int from, int to, int lane) const {
    Segment seg;
    for (int k = from; k < to && k < order.count; ++k)
        if (lane < 0 || order.lane[std::size_t(k)] == lane) seg.idx[seg.n++] = k;
    return seg;
}

bool Rack::runOrder(const RackOrder* order, float* l, float* r, std::size_t n) {
    const int s = order->splitterIndex(), m = order->mixerIndex();
    if (s < 0 || m < 0 || !order->valid()) return run(order, l, r, n, segmentOf(*order, 0, order->count, -1), false);
    // A block is cut into pieces that the lane buffers can hold.
    bool stereo = false;
    for (std::size_t at = 0; at < n; at += std::size_t(maxBlock_)) {
        const std::size_t len = std::min<std::size_t>(std::size_t(maxBlock_), n - at);
        stereo = run(order, l + at, r + at, len, segmentOf(*order, 0, s, -1), stereo);  // before the splitter
        runLanes(*order, l + at, r + at, len, stereo);                                // the two lanes, summed into a pair
        stereo = true;
        stereo = run(order, l + at, r + at, len, segmentOf(*order, m + 1, order->count, -1), stereo);  // after the mixer
    }
    return stereo;
}

bool Rack::runLanes(const RackOrder& order, float* l, float* r, std::size_t n, bool stereo) {
    const int s = order.splitterIndex(), m = order.mixerIndex();
    float* bl = laneBl_.data();
    float* br = laneBr_.data();
    std::copy(l, l + n, bl);
    if (stereo) std::copy(r, r + n, br);
    const Segment a = segmentOf(order, s + 1, m, 0), b = segmentOf(order, s + 1, m, 1);
    const bool stereoA = run(&order, l, r, n, a, stereo);
    const bool stereoB = run(&order, bl, br, n, b, stereo);
    // The lane that is early waits for the other one.
    const int diff = int(std::lround(segmentLatency(order, b) - segmentLatency(order, a)));  // > 0: lane B is later
    const Module* mixer = at(order, m);
    const MixerParams* mp = mixer ? mixer->mixerParams() : nullptr;
    const MixerParams defaults;
    mixLanes(mp ? *mp : defaults, l, r, bl, br, n, stereoA, stereoB, std::clamp(diff, -int(kDelay) + 1, int(kDelay) - 1));
    return true;
}

// Sums lane A (l, r) and lane B (bl, br) into (l, r): the levels, a balance-style pan per lane (0 leaves it as it is), the phase of B, and the
// delay that lines the two up. When the alignment changes the result fades out, the delay changes at silence, and it fades back in.
void Rack::mixLanes(const MixerParams& mp, float* l, float* r, float* bl, float* br, std::size_t n, bool stereoA, bool stereoB, int diffWanted) {
    if (!lanesInit_) {
        for (auto& ring : ring_) std::fill(ring.begin(), ring.end(), 0.0f);
        diffApplied_ = diffWanted;
        mixFade_ = MixFade::Idle;
        lanesInit_ = true;
    }
    auto gains = [](float pan, float& left, float& right) {
        left = pan > 0.0f ? 1.0f - std::min(pan, 1.0f) : 1.0f;
        right = pan < 0.0f ? 1.0f + std::max(pan, -1.0f) : 1.0f;
    };
    float aL, aR, bL, bR;
    gains(mp.panA, aL, aR);
    gains(mp.panB, bL, bR);
    const float la = mp.levelA, lb = mp.levelB * (mp.invertB ? -1.0f : 1.0f);
    const std::size_t mask = kDelay - 1;
    for (std::size_t i = 0; i < n; ++i) {
        if (mixFade_ == MixFade::Idle && diffWanted != diffApplied_) {
            mixFade_ = MixFade::Out;
            mixFadeLeft_ = fadeLen_;
        }
        float g = 1.0f;
        if (mixFade_ != MixFade::Idle) {
            const float t = float(mixFadeLeft_ - 1) / float(fadeLen_);  // 1 -> 0 over a fade
            g = mixFade_ == MixFade::Out ? t : 1.0f - t;
        }
        ring_[0][ringPos_] = l[i];
        ring_[1][ringPos_] = stereoA ? r[i] : l[i];
        ring_[2][ringPos_] = bl[i];
        ring_[3][ringPos_] = stereoB ? br[i] : bl[i];
        const std::size_t dA = diffApplied_ > 0 ? std::size_t(diffApplied_) : 0, dB = diffApplied_ < 0 ? std::size_t(-diffApplied_) : 0;
        const float xAl = ring_[0][(ringPos_ - dA) & mask], xAr = ring_[1][(ringPos_ - dA) & mask];
        const float xBl = ring_[2][(ringPos_ - dB) & mask], xBr = ring_[3][(ringPos_ - dB) & mask];
        l[i] = g * (la * aL * xAl + lb * bL * xBl);
        r[i] = g * (la * aR * xAr + lb * bR * xBr);
        ringPos_ = (ringPos_ + 1) & mask;
        if (mixFade_ != MixFade::Idle && --mixFadeLeft_ <= 0) {
            if (mixFade_ == MixFade::Out) {
                diffApplied_ = diffWanted;  // silence: the new alignment starts here
                mixFade_ = MixFade::In;
                mixFadeLeft_ = fadeLen_;
            } else {
                mixFade_ = MixFade::Idle;
            }
        }
    }
}

bool Rack::run(const RackOrder* order, float* l, float* r, std::size_t n, const Segment& seg, bool stereoIn) {
    bool stereo = stereoIn;
    auto downmix = [&] {
        for (std::size_t i = 0; i < n; ++i) l[i] = 0.5f * (l[i] + r[i]);
        stereo = false;
    };
    const Fusions f = scanFusion(*order, seg);
    if (f.amp >= 0) at(*order, f.amp)->setFrontEnd(f.comp, f.overdrive, f.pedalBefore);
    const bool* fused = f.fused;
    for (int j = 0; j < seg.n; ++j) {
        const int k = seg.idx[j];
        Module* m = at(*order, k);
        if (!m || !m->active() || fused[k]) {
            if (valid(order->slots[std::size_t(k)])) cpuOf(order->slots[std::size_t(k)]).store(0.0f);
            continue;
        }
        const auto t0 = std::chrono::steady_clock::now();
        switch (m->channels()) {
            case Channels::MonoOnly:
                if (stereo) downmix();
                m->process(l, nullptr, n);
                break;
            case Channels::MonoToStereo:
                if (stereo) downmix();
                m->process(l, r, n);
                stereo = true;
                break;
            case Channels::Stereo:
                if (!stereo) {
                    std::copy(l, l + n, r);
                    stereo = true;
                }
                m->process(l, r, n);
                break;
            case Channels::Flexible:
                m->process(l, stereo ? r : nullptr, n);
                break;
        }
        // The share of the block's time this module took, averaged over a few blocks.
        const std::chrono::duration<double> took = std::chrono::steady_clock::now() - t0;
        const float now = float(100.0 * took.count() / (double(n) / fs_));
        auto& avg = cpuOf(order->slots[std::size_t(k)]);
        avg.store(avg.load() * 0.9f + now * 0.1f);
    }
    return stereo;
}

Module* Rack::at(const RackOrder& order, int k) const {
    const SlotRef s = order.slots[std::size_t(k)];
    return valid(s) ? cell(s).load() : nullptr;
}

// A compressor and an overdrive right before the amp of a segment (in that order, by position: whether they are on or not does not matter, so
// switching a neighbour on or off never moves them in or out of the amp) are run by the amp itself, as they always were. A pedal in another
// segment (before the splitter, say) is a pedal of its own and does not count as loading this amp.
Rack::Fusions Rack::scanFusion(const RackOrder& order, const Segment& seg) const {
    Fusions f;
    int ampAt = -1;
    for (int j = 0; j < seg.n; ++j) {
        Module* m = at(order, seg.idx[j]);
        if (m && m->isAmp() && m->active()) {
            f.amp = seg.idx[j];
            ampAt = j;
            break;
        }
    }
    if (f.amp < 0) return f;
    int j = ampAt - 1;
    if (j >= 0)
        if (Module* m = at(order, seg.idx[j]); m && m->fusion() == Fusion::Overdrive) {
            f.overdrive = m;
            f.fused[seg.idx[j--]] = true;
        }
    if (j >= 0)
        if (Module* m = at(order, seg.idx[j]); m && m->fusion() == Fusion::Compressor) {
            f.comp = m;
            f.fused[seg.idx[j]] = true;
        }
    for (int q = 0; q < ampAt; ++q) {
        Module* m = at(order, seg.idx[q]);
        if (!f.fused[seg.idx[q]] && m && m->active() && m->buffered()) f.pedalBefore = true;
    }
    return f;
}

double Rack::segmentLatency(const RackOrder& order, const Segment& seg) const {
    const Fusions f = scanFusion(order, seg);
    double total = 0.0;
    for (int j = 0; j < seg.n; ++j) {
        const Module* m = at(order, seg.idx[j]);
        if (m && !f.fused[seg.idx[j]] && m->active()) total += m->latencySamples();  // a pedal the amp runs has no latency of its own
    }
    return total;
}

double Rack::latencySamples() const {
    const RackOrder* order = current_.load();
    const int s = order->splitterIndex(), m = order->mixerIndex();
    if (s < 0 || m < 0 || !order->valid()) return segmentLatency(*order, segmentOf(*order, 0, order->count, -1));
    return segmentLatency(*order, segmentOf(*order, 0, s, -1)) +
           std::max(segmentLatency(*order, segmentOf(*order, s + 1, m, 0)), segmentLatency(*order, segmentOf(*order, s + 1, m, 1))) +
           segmentLatency(*order, segmentOf(*order, m + 1, order->count, -1));
}

void Rack::reset() {
    for (auto& row : owned_)
        for (auto& m : row)
            if (m) m->reset();
    lanesInit_ = false;
}

}  // namespace sas
