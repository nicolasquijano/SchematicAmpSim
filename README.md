# Schematic Amp Sim

A guitar amp simulator that solves the real circuit instead of imitating it. Each amp is
transcribed from its schematic into a text netlist, and the plugin runs a nonlinear circuit solver
on it in real time: preamp stages, tone stack, phase inverter, output tubes and transformer included.

VST3 for Windows, built with C++17 and [JUCE](https://juce.com).

## Features

- **Circuit-level modelling.** Modified nodal analysis with a DK-style discretisation (trapezoidal
  capacitors, coupled inductors), Newton iteration with line search and a bounded continuation
  fallback. Devices: Koren triodes and pentodes (12AX7-type, EL34, 6550), diodes, BJTs, op-amps.
- **Amps are data, not code.** Each amp is a folder with a netlist and an `amp.json` that maps
  knobs, input jacks, presence, supply and output level onto circuit elements. Adding an amp means
  adding files; nothing is recompiled except to embed them.
- **Three amps included** (display names are deliberately generic):
  | Name | Circuit |
  |---|---|
  | British 800 | Cascading-gain preamp, 100 W-class EL34 power amp, High and Low input jacks |
  | British Plexi | Vintage 100 W-class EL34 head, two channels with jumper |
  | SLSH 100W | Modern 100 W head, 6550 output stage, two modes |
- **Quality modes**
  | Mode | What it does |
  |---|---|
  | Normal | Full circuit, 2x oversampling |
  | High | Full circuit, 4x oversampling |
  | Lite | Full preamp circuit, behavioural power amp fitted against the full circuit |
  | Eco | Lite plus tabulated tube curves and a lighter solver; the multi-stage preamp is split in parts |
- **Cabinet:** off, simple filter, or your own impulse response (stored inside the project).
- **Tube-screamer style pedal** in front of the amp (drive, tone, level).
- **Level matching** across amps, jacks and modes, so switching does not jump in volume.
- Fast parameter smoothing, no allocations on the audio thread; switching amps builds the new
  circuits on a worker thread and swaps them in without stopping audio.

## Parameters

Amp, Volume, Volume 2, Bass, Middle, Treble, Presence, Master, Power, Input, Output, Quality,
Cab (Off / Filter / IR), Jack (1-3), Level match, and the pedal controls (On, Drive, Tone, Level).
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

Windows VST3 only. Planned: Neural Amp Modeler support, AU and AAX. The Eco mode and the level
matching have been verified with tests, not yet extensively by ear.

## Trademarks and license

All product names, trademarks and brands belong to their respective owners. This project is not
affiliated with or endorsed by any amplifier manufacturer; the circuits are independent
transcriptions made for educational and research purposes, and the amp names are generic.

No license has been chosen for this repository yet. Note that JUCE is dual-licensed (AGPLv3 or
commercial), which applies to any distributed build of the plugin.
