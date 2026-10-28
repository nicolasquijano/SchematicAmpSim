# Changelog

All notable changes to Schematic Amp Sim. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the
version numbers follow [Semantic Versioning](https://semver.org/) (see [docs/RELEASING.md](docs/RELEASING.md)).

## [Unreleased]

### Added
- A new amp, British Saturated, from a 1970s 120 W head schematic (preamp, output amp and power supply drawings): the Orange tone circuit, a six-position FAC
  switch (shown as the input list), four EL34 and 15k feedback with a BOOST control. Which EL34 pair drives which end of the transformer is not drawn;
  it is chosen so the feedback is negative. The jack levels of the FAC positions are only partly matched (the amp is saturated). See `amps/orange_or120/*.net`.
- The amp list is scrollable and has a search box above it.
- A new amp, British Hi-Fi, from a 100 W high-headroom head owner's manual (preamp issue 4, output stage, power supply): brilliant and normal channels, passive
  tone stack with master volume, four EL34 and a silicon bridge supply. The printed -38 V bias idles 53 mA per tube with this tube model, so the source
  is set for -43 V (about 38 mA). The plate voltages printed on the preamp are 40 to 75 V lower than the model's; the 100 pF between the preamp's
  last plate and the presence pot is left out (see the comments in `amps/hiwatt_dr103/*.net`).
- A new amp, American Bass 50, from the 5F6-A tweed bass head schematic (Kuehnel's redrawing): bright and normal channels with #1 and #2 inputs,
  two 6L6GC, tube rectifier and choke. The bias source and the rectifier are tuned, not read, because the drawing prints no idle current.
- A new amp, American Triple Stack, from a three-channel solo head schematic (preliminary drawing): channel 1 Clean / Pushed, channels 2 and 3 with
  Raw / Vintage / Modern voicings, four 6L6GC. The relays, the effects loop and the per-channel presence feedback are not modelled; the voicings
  are interpretations of the drawing (see the comments in `amps/triplestack_3ch/*.net`).
- A macOS build (VST3 and Audio Unit, one universal binary for Apple Silicon and Intel, macOS 11 or later), packaged by `package-macos.sh`.
  Not tried on a real Mac yet and not notarized.
- GitHub Actions: `ci.yml` builds and tests on Windows and macOS on every push; `release.yml` builds both and creates a draft release when a
  `v*` tag is pushed.

### Changed
- The input jack selector has 8 positions (it had 4); saved projects keep their selection. Amps list order is unchanged, the new amp is last.
- Strict floating point (`-ffp-contract=off`) on clang and gcc, as `/fp:precise` on MSVC.

## [0.1.0-beta] - 2026-10-27

The first public release: a Windows VST3 with seven circuit-level amplifiers.

### Amps
- Seven amps, each a netlist transcribed from a schematic and solved as a nonlinear circuit in real time: British 800, British Plexi,
  SLSH 100W, American Twin (optical vibrato and spring reverb), American 5050, British Class A and American Tweed.
- Quality modes Normal (2x), High (4x), Lite and Eco, with level matching across amps, jacks and modes.
- Output transformer core saturation, tube and solid-state rectifier supplies with sag, a choke on the Class A amp.
- Data-driven: a new amp is a folder with `amp.json` and its netlists; nothing is recompiled except to embed them.

### Realism
- Output tube choice (EL34, 6L6GC, 6550 where the amp is fixed-bias) re-biased to the same share of the tube's rating, a bias control with
  the plate current read from the circuit, noise and hum that depend on the pickup, wear, and a "unit" number that draws component tolerances.
- Guitar input: single coil, humbucker or P-90 and cable capacitance in front of every amp.
- Glowing power tubes in the editor that follow the plate current.

### Cabinet and stereo
- Cabinet off, a simple filter, or two impulse responses (A and B, each with its response curve) with an equal-power blend; stored in the project.
- Speaker dynamics between the amp and the cabinet: cone excursion, voice-coil heating and break-up, with an Intense mode.
- Fake stereo: impulse A on the left and B on the right (Width), automatic double tracking (Double and Time). A mono track can feed the
  stereo output.

### Effects and tools
- Compressor pedal solved as a circuit from the Keeley Compressor Plus schematic (an OTA gain cell, a transistor envelope detector and the
  feedback that closes the loop), a Tube Screamer style overdrive, a noise gate.
- Chromatic tuner with a needle meter, a reference pitch of 430 to 450 Hz and an option to mute the output.

### Editor
- A list of amps on the left, four pages (Amp, Cabinet, Effects, Tuner), colours and knobs that change with each amp, level meters.

### Known limitations
- Windows (VST3) only. AU and AAX are planned.
- The tube, transformer, rectifier, pickup and noise values are typical figures, not measurements of a particular amp; the schematics
  print none. They are documented next to each netlist.
- The Lite and Eco modes use the stock amp: the output tube choice, wear and unit act in Normal and High only.
- The tuner detects from 60 Hz to about 1.4 kHz.
- While the compressor or the overdrive is on, the pickup and cable of the Guitar Input panel have no effect.
- The saved-project format may still change between 0.x versions.

[0.1.0-beta]: https://github.com/nicolasquijano/SchematicAmpSim/releases/tag/v0.1.0-beta
