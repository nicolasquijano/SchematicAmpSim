#!/usr/bin/env bash
# Packages the macOS build into dist/ (run it on a Mac, after building with `cmake --preset release-macos`):
#   SchematicAmpSim-<version>-macos-universal.zip   the VST3 and Audio Unit bundles, LICENSE, README and INSTALL.txt
#   SHA256SUMS-macos.txt
# The bundles get an ad-hoc signature (Apple Silicon will not run unsigned code). That is NOT the Developer ID signature plus notarization that
# Gatekeeper wants for a download without warnings: see docs/RELEASING.md ("macOS").
set -euo pipefail
cd "$(dirname "$0")"

version=$(sed -n 's/^project(SchematicAmpSim VERSION \([0-9][0-9.]*\).*/\1/p' CMakeLists.txt)
suffix=$(sed -n 's/^set(SAS_VERSION_SUFFIX "\(.*\)")/\1/p' CMakeLists.txt)
[ -n "$version" ] || { echo "could not read the version from CMakeLists.txt" >&2; exit 1; }
full="${version}${suffix}"

artefacts="build/plugin/SchematicAmpSim_artefacts/Release"
vst3="$artefacts/VST3/Schematic Amp Sim.vst3"
au="$artefacts/AU/Schematic Amp Sim.component"
for b in "$vst3" "$au"; do [ -d "$b" ] || { echo "missing bundle: $b" >&2; exit 1; }; done

echo "Packaging Schematic Amp Sim $full for macOS"
for b in "$vst3" "$au"; do
  codesign --force --deep --sign - "$b"
  echo "  $(basename "$b"): $(lipo -archs "$b/Contents/MacOS/"* 2>/dev/null | head -1)"
done

name="Schematic Amp Sim $full"
stage="$(mktemp -d)/$name"
mkdir -p "$stage"
cp -R "$vst3" "$au" "$stage/"
cp LICENSE README.md "$stage/"
cat > "$stage/INSTALL.txt" <<EOF
Schematic Amp Sim $full for macOS (Apple Silicon and Intel, macOS 11 or later)

1. Copy  "Schematic Amp Sim.vst3"      to  /Library/Audio/Plug-Ins/VST3      (or ~/Library/Audio/Plug-Ins/VST3 for your user only)
   Copy  "Schematic Amp Sim.component" to  /Library/Audio/Plug-Ins/Components (or ~/Library/Audio/Plug-Ins/Components)
2. This beta is not notarized by Apple, so macOS may refuse to load it ("damaged" or "unidentified developer"). Remove the quarantine mark:
     xattr -dr com.apple.quarantine "/Library/Audio/Plug-Ins/VST3/Schematic Amp Sim.vst3"
     xattr -dr com.apple.quarantine "/Library/Audio/Plug-Ins/Components/Schematic Amp Sim.component"
   (use the same paths where you copied them)
3. Rescan the plugins in your DAW. In Logic the Audio Unit is under Audio FX > Nico.
EOF

mkdir -p dist
zip="dist/SchematicAmpSim-$full-macos-universal.zip"
rm -f "$zip"
ditto -c -k --keepParent "$stage" "$zip"
echo "  $zip"
shasum -a 256 "$zip" | sed 's#  dist/#  #' > dist/SHA256SUMS-macos.txt
echo "Checksums: dist/SHA256SUMS-macos.txt"
