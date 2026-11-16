#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <vector>

#include "sas/chain.h"
#include "sas/embedded_amps.h"
#include "sas/speaker.h"

namespace {

constexpr double kFs = 48000.0;
constexpr double kPi = 3.14159265358979323846;

std::vector<float> sine(double hz, double amp, double seconds) {
    std::vector<float> v(std::size_t(seconds * kFs));
    for (std::size_t i = 0; i < v.size(); ++i) v[i] = float(amp * std::sin(2.0 * kPi * hz * double(i) / kFs));
    return v;
}

// Magnitude of the `harmonic`-th multiple of `hz` in the last `n` samples (single-bin DFT).
double bin(const std::vector<float>& v, double hz, std::size_t n) {
    double re = 0, im = 0;
    for (std::size_t i = v.size() - n; i < v.size(); ++i) {
        const double ph = 2.0 * kPi * hz * double(i) / kFs;
        re += double(v[i]) * std::cos(ph);
        im += double(v[i]) * std::sin(ph);
    }
    return 2.0 * std::hypot(re, im) / double(n);
}

std::vector<float> run(sas::SpeakerDynamics& s, std::vector<float> in) {
    for (auto& x : in) x = s.process(x);
    return in;
}

double rms(const std::vector<float>& v, std::size_t from, std::size_t to) {
    double e = 0;
    for (std::size_t i = from; i < to; ++i) e += double(v[i]) * v[i];
    return std::sqrt(e / double(to - from));
}

}  // namespace

TEST_CASE("speaker dynamics: amount 0 is a bit-exact bypass") {
    sas::SpeakerDynamics s;
    s.prepare(kFs);
    s.setAmount(0.0, false);
    const auto in = sine(110.0, 0.5, 0.3);
    const auto out = run(s, in);
    CHECK(out == in);
}

TEST_CASE("speaker dynamics: transparent when played softly, compressing and distorting when pushed") {
    const std::size_t n = std::size_t(0.5 * kFs);
    auto measure = [&](double amp, bool intense) {
        sas::SpeakerDynamics s;
        s.prepare(kFs);
        s.setAmount(1.0, intense);
        const auto in = sine(100.0, amp, 1.5);
        const auto out = run(s, in);
        const double gain = bin(out, 100.0, n) / bin(in, 100.0, n);
        const double thd3 = bin(out, 300.0, n) / bin(out, 100.0, n);
        return std::pair{gain, thd3};
    };
    const auto soft = measure(0.01, false);
    const auto hard = measure(0.6, false);
    const auto harder = measure(0.6, true);
    INFO("soft gain " << soft.first << " hd3 " << soft.second << "; hard gain " << hard.first << " hd3 " << hard.second << "; intense gain " << harder.first
                     << " hd3 " << harder.second);
    CHECK(soft.first > 0.99);   // within 0.1 dB at a low level
    CHECK(soft.second < 1e-3);
    CHECK(hard.first < 0.85);   // the motor force falls: more than 1.4 dB of compression
    CHECK(hard.second > 10.0 * soft.second);  // and odd harmonics appear
    CHECK(harder.first < hard.first);
    CHECK(harder.second > hard.second);
}

TEST_CASE("speaker dynamics: a sustained loud signal loses level as the coil heats up") {
    sas::SpeakerDynamics s;
    s.prepare(kFs);
    s.setAmount(1.0, false);
    const auto in = sine(440.0, 0.5, 12.0);  // a mid note: no excursion effect, only heating
    const auto out = run(s, in);
    const double early = rms(out, std::size_t(0.2 * kFs), std::size_t(0.7 * kFs)) / rms(in, std::size_t(0.2 * kFs), std::size_t(0.7 * kFs));
    const double late = rms(out, std::size_t(11.0 * kFs), std::size_t(11.5 * kFs)) / rms(in, std::size_t(11.0 * kFs), std::size_t(11.5 * kFs));
    INFO("gain early " << early << ", after 11 s " << late);
    CHECK(late < early - 0.03);  // at least ~0.3 dB lower
    CHECK(late > 0.6);           // but it is a few dB at most
}

TEST_CASE("speaker dynamics: stays finite on hard random input and recovers when the amount returns to 0") {
    sas::SpeakerDynamics s;
    s.prepare(kFs);
    s.setAmount(1.0, true);
    unsigned state = 12345;
    for (int i = 0; i < 200000; ++i) {
        state = state * 1664525u + 1013904223u;
        const float x = (float(state >> 8) / float(1u << 24) - 0.5f) * 4.0f;
        REQUIRE(std::isfinite(s.process(x)));
    }
    s.setAmount(0.0, true);
    float last = 0.0f;
    for (int i = 0; i < 48000; ++i) last = s.process(0.25f);
    CHECK(last == 0.25f);  // back to an exact bypass
}

TEST_CASE("speaker dynamics in the chain: a pushed amp loses level and an idle control changes nothing") {
    const auto def = sas::loadEmbeddedAmp("jcm800_2203");
    auto level = [&](double speaker) {
        sas::Chain chain(def, kFs);
        sas::ChainSettings s;
        for (const auto& k : def.preamp.knobs) s.knobs.push_back(k.def);
        for (const auto& k : def.power.knobs) s.powerKnobs.push_back(k.def);
        s.presence = def.power.presenceDefault;
        s.quality = sas::Quality::Lite;
        s.cabFilter = false;
        s.speaker = speaker;
        chain.snap(s);
        const auto in = sine(110.0, 0.5, 2.0);
        std::vector<float> out(in.size());
        chain.processAmp(in.data(), out.data(), in.size());
        for (float v : out) REQUIRE(std::isfinite(v));
        return rms(out, out.size() / 2, out.size());
    };
    const double off = level(0.0), on = level(1.0);
    INFO("rms without " << off << ", with " << on);
    CHECK(off > 0.01);
    CHECK(on < off * 0.97);  // the speaker compresses a hard bass note
    CHECK(on > off * 0.3);   // but does not mute it
}

TEST_CASE("speaker dynamics: gain by frequency and level (printed)", "[.speakerbench]") {
    for (double amount : {0.3, 0.6, 1.0}) {
        for (double amp : {0.05, 0.2, 0.5}) {
            std::printf("amount %.1f, amplitude %.2f:", amount, amp);
            for (double hz : {100.0, 200.0, 400.0, 800.0, 1600.0, 3200.0, 6400.0}) {
                sas::SpeakerDynamics s;
                s.prepare(kFs);
                s.setAmount(amount, false);
                const auto out = run(s, sine(hz, amp, 3.0));
                const double g = bin(out, hz, 24000) / amp;
                std::printf(" %5.0f Hz %+5.1f dB |", hz, 20.0 * std::log10(g + 1e-12));
            }
            std::printf("\n");
        }
    }
}

TEST_CASE("speaker dynamics in the chain: how much treble it takes off (printed)", "[.speakerbench]") {
    const auto def = sas::loadEmbeddedAmp("jcm800_2203");
    auto measure = [&](double speaker, bool intense) {
        sas::Chain chain(def, kFs);
        sas::ChainSettings s;
        for (const auto& k : def.preamp.knobs) s.knobs.push_back(k.def);
        for (const auto& k : def.power.knobs) s.powerKnobs.push_back(k.def);
        s.presence = def.power.presenceDefault;
        s.quality = sas::Quality::Lite;
        s.cabFilter = false;
        s.speaker = speaker;
        s.speakerIntense = intense;
        chain.snap(s);
        const auto in = sine(196.0, 0.3, 2.5);
        std::vector<float> out(in.size());
        chain.processAmp(in.data(), out.data(), in.size());
        sas::Biquad hp = sas::Biquad::highpass(float(kFs), 2500.0f, 0.707f);
        double total = 0, high = 0;
        for (std::size_t i = out.size() / 2; i < out.size(); ++i) {
            total += double(out[i]) * out[i];
            const float h = hp.process(out[i]);
            high += double(h) * h;
        }
        const std::size_t n = out.size() - out.size() / 2;
        return std::pair{10.0 * std::log10(total / double(n) + 1e-20), 10.0 * std::log10(high / std::max(total, 1e-20))};
    };
    const auto off = measure(0.0, false);
    for (double amount : {0.0, 0.3, 0.6, 1.0}) {
        const auto r = measure(amount, false);
        std::printf("speaker %.1f: level %+5.1f dB, treble (>2.5 kHz) share %+5.1f dB (change %+4.1f dB, level change %+4.1f dB)\n", amount, r.first, r.second,
                    r.second - off.second, r.first - off.first);
    }
    const auto r = measure(1.0, true);
    std::printf("speaker 1.0 intense: level %+5.1f dB, treble share %+5.1f dB (change %+4.1f dB)\n", r.first, r.second, r.second - off.second);
}
