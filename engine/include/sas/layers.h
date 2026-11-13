#pragma once
// A sustain-layers pedal in the manner of the MXR Layers: it takes a snapshot of what you play and holds it as a layer, a soft pad that sustains under what
// comes next. There are three layers at once, which overlap (Single keeps one). A layer starts in two ways: automatically on a pick attack that goes over the
// Trig threshold (Trig off: never), or by hand (Hold: the layer is held for as long as the switch is on, and when it is released it fades out).
//  - Attack is how long a layer takes to fade in, Decay how long it takes to fade out (down 40 dB); as it fades, the highs go first, as they do on a string.
//  - Sub Oct makes the layers that start from then on play an octave lower (the ones already playing are not changed).
//  - Diffusion smears the layers with a chain of all-pass filters, a texture more than a note.
//  - Stereo spreads the three layers left, centre and right.
// How a layer sounds: the snapshot (200 ms) is played as overlapping grains (100 ms, two heads, Hann windows). Each time a head restarts, its start is the spot
// in the snapshot that best continues what the other head is playing (a least-squares match over a few ms), so a held note stays one steady tone and does not
// flutter. An octave down reads the snapshot at half speed. The dry sound always passes; the layers are added to it by Mix. Mono analysis: both channels are
// summed to make the snapshots.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace sas {

struct LayersParams {
    bool on = false;
    double mix = 0.7;         // 0 .. 1
    double attackS = 0.25;    // 0.02 .. 3
    double decayS = 3.0;      // 0.3 .. 14
    double trig = 0.5;        // 0 .. 1; 0 = no automatic layers
    bool single = false;
    bool subOct = false;
    bool hold = false;        // a layer held by hand (a footswitch held down)
    double diffusion = 0.0;   // 0 .. 1
    bool stereo = false;
};

class Layers {
public:
    static constexpr int kLayers = 3;
    static constexpr double kSnapshotS = 0.2, kGrainS = 0.1;

    void prepare(double fs) {
        fs_ = fs;
        ringSize_ = 1;
        while (ringSize_ < std::size_t(fs * 0.5) + 8) ringSize_ <<= 1;
        ring_.assign(ringSize_, 0.0f);
        ringMask_ = ringSize_ - 1;
        snapLen_ = std::size_t(kSnapshotS * fs);
        grain_ = std::size_t(kGrainS * fs);
        corrLen_ = std::max<std::size_t>(32, std::size_t(0.004 * fs));
        for (auto& ly : layer_) ly.snap.assign(snapLen_ + corrLen_ + 8, 0.0f);
        const int base[4] = {113, 337, 521, 811};
        for (int c = 0; c < 2; ++c)
            for (int k = 0; k < 4; ++k) {
                const std::size_t d = std::max<std::size_t>(8, std::size_t(double(base[k] + 37 * c) * fs / 48000.0));
                ap_[c][k].buf.assign(d, 0.0f);
            }
        reset();
    }
    void reset() {
        std::fill(ring_.begin(), ring_.end(), 0.0f);
        write_ = 0;
        for (auto& ly : layer_) ly = Layer{ly.snap};
        for (auto& c : ap_)
            for (auto& a : c) {
                std::fill(a.buf.begin(), a.buf.end(), 0.0f);
                a.pos = 0;
            }
        envFast_ = envSlow_ = 0.0f;
        sinceTrig_ = 1 << 30;
        pendingIn_ = -1;
        holdSeen_ = false;
        rng_ = 98765u;
    }
    void setParams(const LayersParams& p) {
        const bool wasOn = target_.on;
        if (p.hold && !holdSeen_ && p.on) manualPending_ = true;
        if (!p.hold && holdSeen_) releaseHeld_ = true;
        holdSeen_ = p.hold;
        if (wasOn && !p.on) releaseAll_ = true;
        target_ = p;
    }
    bool active() const {
        if (target_.on) return true;
        for (const auto& ly : layer_)
            if (ly.state != Layer::Idle) return true;
        return false;
    }

    // `r` may be null. The layers are added to the dry sound in both channels.
    void process(float* l, float* r, std::size_t n) {
        if (!active()) return;
        constexpr double kPi = 3.14159265358979323846;
        const float aF = float(1.0 - std::exp(-1.0 / (0.002 * fs_))), aS = float(1.0 - std::exp(-1.0 / (0.08 * fs_)));
        const double decayS = std::clamp(target_.decayS, 0.3, 14.0), attackS = std::clamp(target_.attackS, 0.02, 3.0);
        const float coefDecay = float(std::exp(std::log(0.01) / (decayS * fs_))), coefFast = float(std::exp(std::log(0.01) / (std::min(decayS, 0.6) * 0.25 * fs_)));
        const float coefAttack = float(1.0 - std::exp(-4.0 / (attackS * fs_)));
        const double thr = std::pow(10.0, (-16.0 - 24.0 * std::clamp(target_.trig, 0.0, 1.0)) / 20.0);
        const bool autoTrig = target_.on && target_.trig > 0.02;
        const float mix = float(std::clamp(target_.mix, 0.0, 1.0));
        const double dg = 0.7 * std::clamp(target_.diffusion, 0.0, 1.0);
        if (releaseAll_) {
            releaseAll_ = false;
            for (auto& ly : layer_)
                if (ly.state != Layer::Idle) ly.state = Layer::Release, ly.fast = true;
        }
        if (releaseHeld_) {
            releaseHeld_ = false;
            for (auto& ly : layer_)
                if (ly.held) ly.held = false;
        }
        for (std::size_t i = 0; i < n; ++i) {
            const float in = r ? 0.5f * (l[i] + r[i]) : l[i];
            ring_[write_ & ringMask_] = in;
            ++write_;
            // The pick-attack detector: a fast envelope well above a slow one and above the threshold.
            const float a = std::fabs(in);
            envFast_ += aF * (a - envFast_);
            envSlow_ += aS * (a - envSlow_);
            ++sinceTrig_;
            if (autoTrig && pendingIn_ < 0 && sinceTrig_ > std::size_t(0.15 * fs_) && double(envFast_) > thr && envFast_ > 2.2f * envSlow_) pendingIn_ = long(0.22 * fs_);  // the snapshot is the last 200 ms: it then holds the note and not what was before it
            if (pendingIn_ >= 0 && --pendingIn_ < 0) {
                startLayer(false);
                sinceTrig_ = 0;
            }
            if (manualPending_ && target_.on) {
                manualPending_ = false;
                startLayer(true);
            }
            float wl = 0.0f, wr = 0.0f;
            for (int k = 0; k < kLayers; ++k) {
                Layer& ly = layer_[std::size_t(k)];
                if (ly.state == Layer::Idle) continue;
                float s = grains(ly);
                // The envelope: fade in, hold while held, then fade out (the highs first).
                if (ly.state == Layer::Attack) {
                    ly.gain += (1.0f - ly.gain) * coefAttack * 1.15f;
                    if (ly.gain > 0.98f) {
                        ly.gain = 1.0f;
                        ly.state = Layer::Sustain;
                    }
                } else if (ly.state == Layer::Sustain) {
                    if (!ly.held && !ly.fast) ly.state = Layer::Release;
                    else if (!ly.held) ly.state = Layer::Release;
                } else if (ly.state == Layer::Release) {
                    ly.gain *= ly.fast ? coefFast : coefDecay;
                    if (ly.gain < 0.003f) {
                        ly.state = Layer::Idle;
                        continue;
                    }
                }
                if (ly.held && ly.state == Layer::Release) ly.state = Layer::Sustain;  // grabbed again by the switch
                const float fc = 700.0f + 11000.0f * std::sqrt(std::max(ly.gain, 0.0f));
                const float lp = float(1.0 - std::exp(-2.0 * kPi * double(fc) / fs_));
                ly.lp += lp * (s - ly.lp);
                s = ly.lp * ly.gain;
                if (target_.stereo) {
                    static constexpr float panL[3] = {1.0f, 0.7071f, 0.35f}, panR[3] = {0.35f, 0.7071f, 1.0f};
                    wl += s * panL[k];
                    wr += s * panR[k];
                } else {
                    wl += s * 0.7071f;
                    wr += s * 0.7071f;
                }
            }
            if (dg > 0.0) {
                wl = allpass(0, wl, float(dg));
                wr = allpass(1, wr, float(dg));
            }
            l[i] += wl * mix;
            if (r) r[i] += wr * mix;
        }
    }

private:
    struct Layer {
        enum State { Idle, Attack, Sustain, Release };
        std::vector<float> snap;
        State state = Idle;
        float gain = 0.0f, lp = 0.0f;
        bool held = false, fast = false;
        double rate = 1.0;
        std::size_t t = 0;  // the position in the grain cycle
        double pos[2] = {0.0, 0.0};
        float norm = 1.0f;
    };
    struct Allpass {
        std::vector<float> buf;
        std::size_t pos = 0;
    };

    void startLayer(bool held) {
        // A free layer, or the one that has faded the most.
        int pick = -1;
        for (int k = 0; k < kLayers; ++k)
            if (layer_[std::size_t(k)].state == Layer::Idle) {
                pick = k;
                break;
            }
        if (pick < 0) {
            float best = 2.0f;
            for (int k = 0; k < kLayers; ++k)
                if (layer_[std::size_t(k)].gain < best) best = layer_[std::size_t(k)].gain, pick = k;
        }
        if (target_.single)
            for (int k = 0; k < kLayers; ++k)
                if (k != pick && layer_[std::size_t(k)].state != Layer::Idle) layer_[std::size_t(k)].state = Layer::Release, layer_[std::size_t(k)].fast = true, layer_[std::size_t(k)].held = false;
        Layer& ly = layer_[std::size_t(pick)];
        // The snapshot: the last 200 ms (DC removed), copied out of the ring.
        double mean = 0.0;
        for (std::size_t j = 0; j < snapLen_; ++j) {
            const float v = ring_[(write_ - snapLen_ + j) & ringMask_];
            ly.snap[j] = v;
            mean += double(v);
        }
        mean /= double(snapLen_);
        for (std::size_t j = 0; j < snapLen_; ++j) ly.snap[j] -= float(mean);
        for (std::size_t j = snapLen_; j < ly.snap.size(); ++j) ly.snap[j] = 0.0f;
        ly.state = Layer::Attack;
        ly.gain = 0.0f;
        ly.lp = 0.0f;
        ly.held = held;
        ly.fast = false;
        ly.rate = target_.subOct ? 0.5 : 1.0;
        ly.t = 0;
        const double span = double(snapLen_ - std::size_t(double(grain_) * ly.rate));
        ly.pos[0] = 0.0;
        ly.pos[1] = std::min(span, 0.5 * span);
    }

    float rand01() {
        rng_ = rng_ * 1664525u + 1013904223u;
        return float(rng_ >> 8) * (1.0f / 16777216.0f);
    }
    static float readSnap(const Layer& ly, double pos) {
        const std::size_t i = std::size_t(pos);
        const float f = float(pos - double(i));
        return ly.snap[i] + (ly.snap[i + 1] - ly.snap[i]) * f;
    }

    // One sample of a layer: two Hann-windowed grain heads half a grain apart.
    float grains(Layer& ly) {
        const std::size_t half = grain_ / 2;
        float out = 0.0f;
        for (int h = 0; h < 2; ++h) {
            const std::size_t ph = (ly.t + std::size_t(h) * half) % grain_;
            if (ph == 0 && ly.t + std::size_t(h) * half != 0) restartHead(ly, h);
            const float w = 0.5f - 0.5f * std::cos(6.283185307179586f * float(ph) / float(grain_));
            out += w * readSnap(ly, ly.pos[h]);
            ly.pos[h] += ly.rate;
            if (ly.pos[h] >= double(snapLen_)) ly.pos[h] = double(snapLen_) - 1.0;
        }
        ly.t = (ly.t + 1) % grain_;
        return out;
    }

    // A head starts a new grain: the start of the snapshot that best continues the other head.
    void restartHead(Layer& ly, int h) {
        const std::size_t span = snapLen_ - std::size_t(double(grain_) * ly.rate);
        const double other = ly.pos[1 - h];
        if (ly.rate < 0.75) {  // an octave down: there is no tone to follow at half speed, any start will do
            ly.pos[h] = double(rand01()) * double(span);
            return;
        }
        const std::size_t o = std::min(std::size_t(other), snapLen_ - 1);
        std::size_t best = 0;
        double bestErr = 1e30;
        for (std::size_t c = 0; c <= span; c += 2) {
            double e = 0.0;
            for (std::size_t k = 0; k < corrLen_; k += 2) {
                const float d = ly.snap[c + k] - ly.snap[std::min(o + k, snapLen_ - 1)];
                e += double(d) * d;
            }
            if (e < bestErr) bestErr = e, best = c;
        }
        ly.pos[h] = double(best);
    }

    float allpass(int c, float x, float g) {
        for (int k = 0; k < 4; ++k) {
            Allpass& a = ap_[c][k];
            const float d = a.buf[a.pos];
            const float y = -g * x + d;
            a.buf[a.pos] = x + g * y;
            a.pos = (a.pos + 1) % a.buf.size();
            x = y;
        }
        return x;
    }

    double fs_ = 48000.0;
    std::size_t ringSize_ = 0, ringMask_ = 0, write_ = 0, snapLen_ = 0, grain_ = 0, corrLen_ = 0;
    std::vector<float> ring_;
    std::array<Layer, kLayers> layer_;
    Allpass ap_[2][4];
    LayersParams target_;
    float envFast_ = 0.0f, envSlow_ = 0.0f;
    std::size_t sinceTrig_ = 1 << 30;
    long pendingIn_ = -1;
    bool manualPending_ = false, releaseHeld_ = false, releaseAll_ = false, holdSeen_ = false;
    std::uint32_t rng_ = 98765u;
};

}  // namespace sas
