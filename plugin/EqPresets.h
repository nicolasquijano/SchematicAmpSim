#pragma once
// The factory presets of the post equaliser (only the EQ: the amp, the effects and the cabinet are left as they are).
// Band 1 is the low shelf, 2 to 4 are bells, 5 is the high shelf; a high-pass and a low-pass at 20 Hz / 20 kHz are off.
// They are made for the guitar after a cabinet: the high-pass takes the rumble under the low E, the low-pass the fizz over the speaker's range.

#include <array>

#include "sas/eq.h"

struct EqPreset {
    const char* name;
    double hpfHz, lpfHz;
    std::array<sas::EqBand, 5> band;  // frequency, gain in dB, Q
};

inline constexpr EqPreset kEqPresets[] = {
    {"Flat", 20.0, 20000.0, {{{100.0, 0.0, 0.7}, {250.0, 0.0, 1.0}, {1000.0, 0.0, 1.0}, {3500.0, 0.0, 1.0}, {6000.0, 0.0, 0.7}}}},
    // Clean and chime: mud out of the low mids, air and sparkle on top.
    {"Clean sparkle", 70.0, 20000.0, {{{120.0, -1.5, 0.7}, {250.0, -2.0, 1.0}, {1500.0, 1.0, 0.9}, {3200.0, 2.0, 1.0}, {9000.0, 3.0, 0.7}}}},
    // Round and warm for blues and classic rock: weight in the lows, the harsh top rolled off.
    {"Warm blues", 60.0, 9000.0, {{{150.0, 2.0, 0.7}, {400.0, 1.5, 0.9}, {1000.0, 0.0, 1.0}, {3000.0, -1.5, 1.0}, {6000.0, -3.0, 0.7}}}},
    // High-gain rhythm: tight lows, the mud cut, the pick attack kept, the fizz trimmed.
    {"Tight rhythm", 90.0, 7500.0, {{{120.0, -2.0, 0.7}, {300.0, -3.0, 1.1}, {1200.0, 1.5, 1.0}, {3500.0, 2.0, 1.2}, {6500.0, -3.0, 0.7}}}},
    // Lead: the mids that make a solo sing and cut through a band.
    {"Lead cut-through", 110.0, 9500.0, {{{150.0, -2.0, 0.7}, {800.0, 3.0, 1.1}, {2200.0, 2.5, 1.0}, {5000.0, -1.5, 1.5}, {7500.0, -2.5, 0.7}}}},
    // For a harsh amp or a bright speaker: the 3 to 5 kHz edge and the top tamed, a little warmth added.
    {"Smooth (de-fizz)", 80.0, 6500.0, {{{100.0, -1.0, 0.7}, {250.0, -1.5, 1.0}, {1000.0, 1.0, 1.0}, {4500.0, -3.0, 1.5}, {7000.0, -4.0, 0.7}}}},
};
inline constexpr int kEqPresetCount = int(sizeof(kEqPresets) / sizeof(kEqPresets[0]));
