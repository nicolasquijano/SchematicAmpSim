#include "sas/rack.h"

#include <algorithm>
#include <chrono>

namespace sas {

namespace {
constexpr const char* kNames[kModuleTypeCount] = {"Amp", "Cabinet", "Gate", "Compressor", "Overdrive", "NamPedal", "Phaser", "Chorus", "Vibe", "Pitch", "Eq", "Delay", "Reverb"};
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
    for (auto& m : owned_) m->prepare(fs, maxBlock);
}

void Rack::setModule(SlotRef slot, std::unique_ptr<Module> module) {
    if (!valid(slot)) return;
    retired_.clear();  // retired a call ago: the audio thread has long left them
    if (module) module->prepare(fs_, maxBlock_);
    Module* old = cell(slot).exchange(module.get());
    if (old)
        for (auto& o : owned_)
            if (o.get() == old) {
                retired_.push_back(std::move(o));
                break;
            }
    owned_.erase(std::remove(owned_.begin(), owned_.end(), nullptr), owned_.end());
    if (module) owned_.push_back(std::move(module));
}

void Rack::removeModule(SlotRef slot) { setModule(slot, nullptr); }

Module* Rack::module(SlotRef slot) const { return valid(slot) ? cell(slot).load() : nullptr; }

void Rack::publish(const RackOrder& order, Transition how) {
    nextOrder_ = (nextOrder_ + 1) % orders_.size();
    orders_[nextOrder_] = order;
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
        const bool stereo = run(applied_, l + done, r + done, len);
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
    return anyStereo;
}

bool Rack::run(const RackOrder* order, float* l, float* r, std::size_t n) {
    bool stereo = false;
    auto downmix = [&] {
        for (std::size_t i = 0; i < n; ++i) l[i] = 0.5f * (l[i] + r[i]);
        stereo = false;
    };
    auto at = [&](int k) -> Module* {
        const SlotRef s = order->slots[std::size_t(k)];
        return valid(s) ? cell(s).load() : nullptr;
    };
    // The pedals in front of the amp: up to a compressor and an overdrive (in that order) right before it go into the amp's front end.
    bool fused[RackOrder::kMax] = {};
    for (int a = 0; a < order->count; ++a) {
        Module* amp = at(a);
        if (!amp || !amp->isAmp() || !amp->active()) continue;
        Module *comp = nullptr, *ts = nullptr;
        for (int j = a - 1; j >= 0; --j) {
            Module* m = at(j);
            if (!m || !m->active()) continue;
            const Fusion f = m->fusion();
            if (f == Fusion::Overdrive && !ts && !comp) {
                ts = m;
                fused[j] = true;
                continue;
            }
            if (f == Fusion::Compressor) {
                comp = m;
                fused[j] = true;
            }
            break;
        }
        bool pedalBefore = false;
        for (int j = 0; j < a; ++j) {
            Module* m = at(j);
            if (!fused[j] && m && m->active() && m->buffered()) pedalBefore = true;
        }
        amp->setFrontEnd(comp, ts, pedalBefore);
        break;
    }
    for (int k = 0; k < order->count; ++k) {
        Module* m = at(k);
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

double Rack::latencySamples() const {
    const RackOrder* order = current_.load();
    double total = 0.0;
    for (int k = 0; k < order->count; ++k) {
        const SlotRef s = order->slots[std::size_t(k)];
        const Module* m = valid(s) ? cell(s).load() : nullptr;
        if (m && m->active()) total += m->latencySamples();
    }
    return total;
}

void Rack::reset() {
    for (auto& m : owned_) m->reset();
}

}  // namespace sas
