#pragma once
// A harmonizer in the manner of the Boss PS-6 Harmonist: two voices shifted from the note being played, mixed with the guitar.
//   Harmony  each voice is an interval counted in steps of a musical scale in the chosen key (+2 is a third above, +4 a fifth above, -2 a third
//            below...), so the harmony stays in key: a third above is a major or minor third depending on the note played. The note is found by a
//            pitch detector (YIN on a decimated copy of the signal), so this mode is monophonic: one note at a time.
//   Pitch    each voice is shifted by a fixed number of semitones (no detection: chords work).
//   Detune   voice 1 a few cents sharp and voice 2 as many flat, for a thick doubled sound (no detection).
// The voices are made by the granular pitch shifter of the rack (sas/pitch_shifter.h); the guitar is delayed by the same time so that it lines up with
// them. While it is on, the module adds that delay (about 12 ms). Switching it on or off fades over 20 ms.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

#include "sas/pitch_shifter.h"

namespace sas {

struct HarmonistParams {
    bool on = false;
    int mode = 0;          // 0 Harmony, 1 Pitch, 2 Detune
    int key = 0;           // 0 = C ... 11 = B
    int scale = 0;         // 0 Major, 1 Minor, 2 Harmonic minor, 3 Dorian, 4 Mixolydian
    double interval1 = 2;  // Harmony: steps of the scale (-8 .. 8); Pitch: semitones (-12 .. 12)
    double interval2 = 4;
    double detuneCents = 15;  // Detune: 0 .. 50
    double direct = 1.0;      // 0 .. 1
    double voice1 = 0.8;
    double voice2 = 0.0;
};

namespace harmonist {

inline constexpr int kScales[5][7] = {{0, 2, 4, 5, 7, 9, 11}, {0, 2, 3, 5, 7, 8, 10}, {0, 2, 3, 5, 7, 8, 11}, {0, 2, 3, 5, 7, 9, 10}, {0, 2, 4, 5, 7, 9, 10}};

// The shift in semitones that makes `steps` scale steps above (or below) the note `midi`, in the key and the scale. A note that is not in the scale
// counts as its nearest scale note.
inline double harmonyShift(int midi, int key, int scale, int steps) {
    const int* s = kScales[std::clamp(scale, 0, 4)];
    const int rel = midi - key;
    const int pc = ((rel % 12) + 12) % 12;
    int deg = 0, bestDist = 99;
    for (int d = 0; d < 7; ++d) {
        const int dist = std::abs(s[d] - pc);
        if (dist < bestDist) {
            bestDist = dist;
            deg = d;
        }
    }
    const int t = deg + steps;
    const int oct = int(std::floor(double(t) / 7.0));
    const int td = t - 7 * oct;
    return double(s[td] + 12 * oct - pc);
}

}  // namespace harmonist

// Finds the note being played: YIN on a copy of the signal brought down to about 12 kHz. One estimate every ~5 ms.
class NoteTracker {
public:
    void prepare(double fs) {
        decim_ = std::max(1, int(std::lround(fs / 12000.0)));
        fsd_ = fs / double(decim_);
        a_ = float(1.0 - std::exp(-6.283185307179586 * 1300.0 / fs));
        hop_ = std::max(16, int(fsd_ * 0.005));
        window_ = std::max(64, int(fsd_ * 0.032));
        tauMax_ = int(fsd_ / 70.0);
        tauMin_ = std::max(2, int(fsd_ / 1000.0));
        buf_.assign(std::size_t(window_ + tauMax_ + 8), 0.0f);
        d_.assign(std::size_t(tauMax_ + 2), 0.0f);
        reset();
    }
    void reset() {
        std::fill(buf_.begin(), buf_.end(), 0.0f);
        lp_[0] = lp_[1] = 0.0f;
        phase_ = 0;
        sinceHop_ = 0;
        note_ = -1;
        candidate_ = -1;
        candidateCount_ = 0;
        silentHops_ = 1000;
    }
    // Feed one sample. Returns true when a new estimate was made.
    bool push(float x) {
        lp_[0] += a_ * (x - lp_[0]);
        lp_[1] += a_ * (lp_[0] - lp_[1]);
        if (++phase_ < decim_) return false;
        phase_ = 0;
        // The buffer is a plain shift register of decimated samples: newest at the end.
        std::copy(buf_.begin() + 1, buf_.end(), buf_.begin());
        buf_.back() = lp_[1];
        if (++sinceHop_ < hop_) return false;
        sinceHop_ = 0;
        estimate();
        return true;
    }
    // The note (MIDI number) being played, or -1 when there is none.
    int note() const { return note_; }

private:
    void estimate() {
        const int n = int(buf_.size());
        const int w = window_;
        const float* x = buf_.data() + (n - w - tauMax_);  // the last w + tauMax samples
        double energy = 0.0;
        for (int j = 0; j < w; ++j) energy += double(x[tauMax_ + j]) * x[tauMax_ + j];
        if (energy / double(w) < 1e-6) {  // nothing is playing
            if (++silentHops_ > 20) note_ = candidate_ = -1;
            return;
        }
        // The difference function over the lags and its cumulative mean (YIN).
        double running = 0.0;
        for (int tau = 1; tau <= tauMax_; ++tau) {
            double sum = 0.0;
            for (int j = 0; j < w; ++j) {
                const float diff = x[tauMax_ + j] - x[tauMax_ + j - tau];
                sum += double(diff) * diff;
            }
            running += sum;
            d_[std::size_t(tau)] = float(running > 0.0 ? sum * double(tau) / running : 1.0);
        }
        int best = -1;
        for (int tau = tauMin_; tau < tauMax_; ++tau) {
            if (d_[std::size_t(tau)] < 0.15f) {
                while (tau + 1 < tauMax_ && d_[std::size_t(tau + 1)] < d_[std::size_t(tau)]) ++tau;  // down to the bottom of the dip
                best = tau;
                break;
            }
        }
        if (best < 0) {  // no clear period: it is not a note (a chord, noise): keep the last note for a while
            if (++silentHops_ > 40) note_ = candidate_ = -1;
            return;
        }
        double tau = double(best);
        if (best > 1 && best + 1 <= tauMax_) {  // parabolic refinement
            const double a = d_[std::size_t(best - 1)], b = d_[std::size_t(best)], c = d_[std::size_t(best + 1)];
            const double den = a - 2.0 * b + c;
            if (std::fabs(den) > 1e-9) tau += 0.5 * (a - c) / den;
        }
        silentHops_ = 0;
        const double f = fsd_ / tau;
        const int midi = int(std::lround(69.0 + 12.0 * std::log2(f / 440.0)));
        if (midi == candidate_) {
            if (++candidateCount_ >= 2) note_ = midi;  // the same note twice in a row: it is that one
        } else {
            candidate_ = midi;
            candidateCount_ = 1;
        }
    }

    int decim_ = 4, hop_ = 60, window_ = 384, tauMax_ = 171, tauMin_ = 12;
    double fsd_ = 12000.0;
    float a_ = 0.15f;
    float lp_[2] = {};
    std::vector<float> buf_, d_;
    int phase_ = 0, sinceHop_ = 0, note_ = -1, candidate_ = -1, candidateCount_ = 0, silentHops_ = 1000;
};

class Harmonist {
public:
    static constexpr double kGrainMs = 40.0;  // longer than the rack pitch shifter's 20 ms, so the low strings stay whole
    void prepare(double fs) {
        fs_ = fs;
        for (auto& s : shifter_) s.prepare(fs, kGrainMs);
        tracker_.prepare(fs);
        delay_.assign(std::size_t(shifter_[0].latencySamples()) + 8, 0.0f);
        reset();
    }
    void reset() {
        for (auto& s : shifter_) s.reset();
        tracker_.reset();
        std::fill(delay_.begin(), delay_.end(), 0.0f);
        wr_ = 0;
        shift_[0] = shift_[1] = 0.0;
        amount_ = target_.on ? 1.0 : 0.0;
    }
    void setParams(const HarmonistParams& p) {
        if (!ready_) {
            amount_ = p.on ? 1.0 : 0.0;
            ready_ = true;
        }
        if (p.on && !target_.on) reset();  // it starts clean
        target_ = p;
    }
    bool active() const { return target_.on || amount_ > 1e-4; }
    double latencySamples() const { return std::floor(shifter_[0].latencySamples()); }

    void process(float* l, float* /*r*/, std::size_t n) {
        if (!active()) return;
        const double kAmount = 1.0 - std::exp(-1.0 / (0.02 * fs_));
        const int mode = std::clamp(target_.mode, 0, 2);
        const bool detect = mode == 0 && (target_.voice1 > 0.001 || target_.voice2 > 0.001);
        const std::size_t lat = std::size_t(shifter_[0].latencySamples());
        const std::size_t size = delay_.size();
        for (std::size_t i = 0; i < n; ++i) {
            const float in = l[i];
            if (detect) tracker_.push(in);
            if (i % 32 == 0) updateShifts(mode);
            delay_[wr_] = in;
            const float dry = delay_[(wr_ + size - lat) % size];
            wr_ = (wr_ + 1) % size;
            float v[2];
            for (int k = 0; k < 2; ++k) shifter_[k].process(&in, &v[k], 1, PitchShifter::ratioFromSemitones(shift_[k]));
            amount_ += ((target_.on ? 1.0 : 0.0) - amount_) * kAmount;
            const float wet = float(target_.direct) * dry + float(target_.voice1) * v[0] + float(target_.voice2) * v[1];
            const float a = float(amount_);
            l[i] = in * (1.0f - a) + wet * a;  // (the guitar is the same sound earlier: the crossfade only matters while it fades)
        }
    }

private:
    void updateShifts(int mode) {
        if (mode == 1) {
            shift_[0] = std::clamp(target_.interval1, -12.0, 12.0);
            shift_[1] = std::clamp(target_.interval2, -12.0, 12.0);
        } else if (mode == 2) {
            shift_[0] = std::clamp(target_.detuneCents, 0.0, 50.0) / 100.0;
            shift_[1] = -shift_[0];
        } else {
            const int note = tracker_.note();
            if (note >= 0) {
                shift_[0] = harmonist::harmonyShift(note, target_.key, target_.scale, int(std::lround(std::clamp(target_.interval1, -8.0, 8.0))));
                shift_[1] = harmonist::harmonyShift(note, target_.key, target_.scale, int(std::lround(std::clamp(target_.interval2, -8.0, 8.0))));
            }  // no note: the voices stay where they were
        }
    }

    double fs_ = 48000.0;
    std::array<PitchShifter, 2> shifter_;
    NoteTracker tracker_;
    std::vector<float> delay_;
    std::size_t wr_ = 0;
    double shift_[2] = {0.0, 0.0};
    double amount_ = 0.0;
    HarmonistParams target_;
    bool ready_ = false;
};

}  // namespace sas
