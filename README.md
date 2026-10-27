# Schematic Amp Sim

A guitar amp simulator that solves the real circuit instead of imitating it. Each amp is
transcribed from its schematic into a text netlist, and the plugin runs a nonlinear circuit solver
on it in real time: preamp stages, tone stack, phase inverter, output tubes and transformer included.

VST3 for Windows, built with C++17 and [JUCE](https://juce.com).

## Features

- **Circuit-level modelling.** Modified nodal analysis with a DK-style discretisation (trapezoidal
  capacitors, coupled inductors), Newton iteration with line search and a bounded continuation
  fallback. Devices: Koren triodes and pentodes (12AX7-type, EL34, EL84, 6V6, 6L6, 6550), diodes, BJTs, op-amps.
- **Amps are data, not code.** Each amp is a folder with a netlist and an `amp.json` that maps
  knobs, input jacks, presence, supply and output level onto circuit elements. Adding an amp means
  adding files; nothing is recompiled except to embed them.
- **Five amps included** (display names are deliberately generic):
  | Name | Circuit |
  |---|---|
  | British 800 | Cascading-gain preamp, 100 W-class EL34 power amp, High and Low input jacks |
  | British Plexi | Vintage 100 W-class EL34 head, two channels with jumper |
  | SLSH 100W | Modern 100 W head, 6550 output stage, two modes |
  | American Twin | Two-channel 85 W+ blackface-style amp, 12AT7 phase inverter, four 6L6GC on fixed bias with its optical vibrato and spring reverb (driver and recovery tubes as circuit, the spring tank behavioural) |
  | American 5050 | Modern high-gain 120 W head: two channels (Rhythm with a Crunch voicing, Lead) through six gain stages, Marshall-type tone stack, 12AX7 phase inverter with presence and resonance in the feedback loop, four 6L6GC |
  | British Class A | Class A 30 W combo-style amp (Top Boost reissue drawing): Normal channel straight to the phase inverter, Brilliant channel through the Treble / Bass network, Cut control, four cathode-biased EL84 with no negative feedback (no vibrato or reverb) |
  | American Tweed | 1950s-style 12 W tweed combo: two channels with interactive volume pots and a tone control, 12AY7 / 12AX7 preamp, cathodyne phase inverter, two cathode-biased 6V6GT, no negative feedback, high-impedance 5Y3GT rectifier supply |
- **Quality modes**
  | Mode | What it does |
  |---|---|
  | Normal | Full circuit, 2x oversampling |
  | High | Full circuit, 4x oversampling |
  | Lite | Full preamp circuit, behavioural power amp fitted against the full circuit |
  | Eco | Lite plus tabulated tube curves and a lighter solver; the multi-stage preamp is split in parts |
- **Amp condition** (Effects page): output-tube type (EL34 / 6L6GC / 6550 on the amps with a fixed bias,
  re-biased to the same share of the tube's dissipation rating), a bias control with the plate current in mA
  read from the circuit, noise and hum (hiss at the input, mains hum that depends on the pickup), wear
  (emission loss of the tubes, capacitance loss of the electrolytics and the reservoir) and a "unit" number
  that draws component tolerances. Tubes, wear and unit rebuild the circuits, so they act in the Normal and
  High modes only; the Lite fit belongs to the stock amp. The hiss and hum levels, the wear laws and the
  tolerances are typical figures, not measurements of a particular amp (see `engine/include/sas/noise.h` and
  `engine/src/condition.cpp`). `tools/sas_bias_cal` computes the `bias_scale` of each alternative tube.
- **Cabinet page:** off, a simple filter, or two impulse responses (A and B, each with its response curve) mixed with an equal-power blend; both are stored inside the project. Speaker dynamics sit between the amp and the cabinet: cone excursion (the motor force falls and the suspension stiffens as the cone is pushed), voice-coil heating and cone break-up, with an Intense mode. The values are typical figures, tuned by ear. **Stereo** (the amp is mono inside): the Width knob puts impulse A in the left ear and B in the right one (two microphones, one on each side; 0 blends both into both ears), and Double adds automatic double tracking, a second take with a different, slowly modulated delay in each ear (8 to 30 ms), which also works with the cabinet filter or a single impulse. Both are off by default, which leaves the same signal in both ears. A mono track can feed the stereo output.
- **Tube-screamer style pedal** in front of the amp (drive, tone, level).
- **Compressor pedal** (Effects page, in front of everything else), solved as a circuit from a transcription of the Keeley Compressor Plus schematic (AionFX trace v1.0, `engine/data/compressor.net`): an LM13700 OTA gain cell, a transistor full-wave envelope detector and the feedback that closes the loop, with Sustain, Blend, Tone, Level and the Attack switch (which in the circuit selects fast or slow release). It needed two new devices in the engine: an Ebers-Moll NPN that saturates and the OTA. It costs about 15 % of a core while on. The transistor, OTA and diode values are typical figures; the drawing prints none.
- **Level matching** across amps, jacks and modes, so switching does not jump in volume.
- Fast parameter smoothing, no allocations on the audio thread; switching amps builds the new
  circuits on a worker thread and swaps them in without stopping audio.

## Parameters

Amp, Volume, Volume 2, Bass, Middle, Treble, Presence, Master, Power, Input, Output, Quality,
Cab (Off / Filter / IR), Jack (1-4), Level match, Vibrato (On, Speed, Intensity) and Reverb and Resonance (amps that have them), the pedal controls (On, Drive, Tone, Level), the noise gate, the guitar input (pickup, cable) and the amp condition (Power tubes, Bias, Noise and hum, Wear, Unit).
Which knobs apply depends on the selected amp.

## Building

Requirements: Windows 10/11, Visual Studio 2022 Build Tools (MSVC), Git. CMake and Ninja are
taken from the Build Tools. Dependencies (JUCE 8.0.16, Catch2 3.16.1, nlohmann/json 3.11.3) are
fetched by CMake into `third_party/` on first configure.

```bat
build.cmd              :: configure and build everything
build.cmd test         :: ... and run the tests
build.cmd target sas_tests
```

The plugin ends up in `build/plugin/SchematicAmpSim_artefacts/Release/VST3/`. To install, copy the
`Schematic Amp Sim.vst3` bundle contents to
`%LOCALAPPDATA%\Programs\Common\VST3\Schematic Amp Sim.vst3` (or `C:\Program Files\Common Files\VST3`)
with the DAW closed, then rescan plugins.

## Repository layout

```
engine/    solver library, no JUCE dependency (linear algebra, devices, circuit, netlist, oversampling, chain)
amps/      one folder per amp: amp.json + .net files, embedded into the build
plugin/    JUCE processor, editor, parameters, cabinet convolution
tests/     Catch2 unit tests and parity tests against reference data (55 tests)
tools/     sas_bench, sas_inspect, sas_fit_lite, sas_match_levels
golden/    reference output from the original Rust implementation
```

## Adding an amp

1. Create `amps/<id>/` with `preamp.net`, `poweramp.net` and `amp.json`.
2. Write the netlist (R, C, T triode, P pentode, D, Q, O op-amp, V/I sources, `.param`, `.xfmr`).
   Values accept SI suffixes and RKM notation (`4k7`, `2n2`).
3. In `amp.json`, map the knobs (pot type, taper, total resistance), jacks, presence and supply.
4. Run `sas_inspect <id>` to check DC operating points, then `sas_fit_lite <id>` to fit the Lite power
   amp and `sas_match_levels` to set the output trim.
5. Rebuild: amps are discovered by globbing `amps/*/amp.json` and embedded automatically.

## Status

Windows VST3 only. Planned: AU and AAX. The Eco mode and the level
matching have been verified with tests, not yet extensively by ear.

## License and trademarks

All product names, trademarks and brands belong to their respective owners. This project is not
affiliated with or endorsed by any amplifier manufacturer; the circuits are independent
transcriptions made for educational and research purposes, and the amp names are generic.

Free software, licensed under the [GNU Affero General Public License v3.0](LICENSE). Contributions
are welcome under the same license. The plugin is built on JUCE (used under AGPLv3), Catch2 (BSL-1.0)
and nlohmann/json (MIT).
