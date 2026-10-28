# Releasing

## Versions

[Semantic Versioning](https://semver.org/): `MAJOR.MINOR.PATCH`, with a `-beta` suffix while the saved-project format may still change.

| Change | Bumps | Example |
|---|---|---|
| Fixes, small adjustments, no new parameters | PATCH | 0.1.0-beta -> 0.1.1-beta |
| A new amp, effect or page; new parameters | MINOR | 0.1.1-beta -> 0.2.0-beta |
| The first version that promises compatibility | MAJOR 1, no suffix | 0.x-beta -> 1.0.0 |
| After 1.0: a change that breaks saved projects | MAJOR | 1.4.0 -> 2.0.0 |

The version is written in **one place**: `VERSION` and `SAS_VERSION_SUFFIX` at the top of `CMakeLists.txt`. CMake generates
`sas/version_info.h` from it; the engine (`sas::engineVersion()`), the VST3 and the footer of the editor all read that. `package.ps1` reads the
same lines for the file names and the installer.

Git tags are `v<version>` (for example `v0.1.0-beta`). A `-beta` version is published on GitHub as a pre-release.

### Compatibility of saved projects (what a DAW project stores)

A host stores every parameter by its **id** and its value. From 1.0 on, and as far as possible before:

- never rename or remove a parameter id, and never reuse one for something else;
- a choice parameter keeps the order of its entries: new entries are only appended (Quality, Cabinet and the tube types already work this way);
- the meaning of a value does not change (a 0..10 knob stays a 0..10 knob);
- new parameters need a default that leaves the sound as it was (the stereo, the compressor and the tuner are all off by default);
- the project state also carries the impulse responses (`irData`, `irDataB`): keep reading those keys.

If something has to break, bump MAJOR (or MINOR while 0.x) and say so in the changelog.

## Steps of a release

1. **Prepare**: the working tree is clean (`git status`), the tests pass (`build.cmd test`).
2. **Version**: edit `VERSION` (and `SAS_VERSION_SUFFIX`) in `CMakeLists.txt`.
3. **Changelog**: in `CHANGELOG.md` add a section `## [x.y.z] - YYYY-MM-DD` with what changed (Added, Changed, Fixed, Known limitations), and the
   link at the bottom of the file.
4. **Commit**: `git commit -am "Release x.y.z"`.
5. **Package**: `package.cmd` builds, runs the tests and writes `dist\`: the zip of the VST3 bundle, the installer (when Inno Setup 6 is
   installed) and `SHA256SUMS.txt`. Try the installer on a machine or in a DAW that does not have the plugin yet.
6. **Tag**: `git tag -a vx.y.z -m "Schematic Amp Sim x.y.z"`.
7. **Push**: `git push origin master --tags`.
8. **GitHub release** (Releases > Draft a new release): choose the tag, title `Schematic Amp Sim x.y.z`, paste the changelog section as the notes,
   attach the files of `dist\`, tick "pre-release" while the version has the `-beta` suffix.

## Do not commit

Build output (`build/`, `build-*/`, `dist/`) and the dependencies fetched into `third_party/` are in `.gitignore`. Check `git status` before
`git add -A`: an experiment folder at the top of the tree is the usual way a few hundred megabytes end up in the history.

## The installer

`installer/schematic-amp-sim.iss` (Inno Setup 6) installs the bundle into `C:\Program Files\Common Files\VST3\Schematic Amp Sim.vst3`, shows
the license, and registers an uninstaller. The `AppId` GUID in it identifies the product so that a newer installer replaces the older install:
never change it. The installer is not code-signed, so Windows SmartScreen may warn when it is run for the first time.
