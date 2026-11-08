#!/usr/bin/env bash
# Packages the Linux build into dist/ (run it on Linux, after building with `cmake --preset release-linux`):
#   SchematicAmpSim-<version>-linux-x86_64.tar.gz   the VST3 bundle and the standalone application, LICENSE, README and INSTALL.txt
#   SHA256SUMS-linux.txt
set -euo pipefail
cd "$(dirname "$0")"

version=$(sed -n 's/^project(SchematicAmpSim VERSION \([0-9][0-9.]*\).*/\1/p' CMakeLists.txt)
suffix=$(sed -n 's/^set(SAS_VERSION_SUFFIX "\(.*\)")/\1/p' CMakeLists.txt)
[ -n "$version" ] || { echo "could not read the version from CMakeLists.txt" >&2; exit 1; }
full="${version}${suffix}"

artefacts="build/plugin/SchematicAmpSim_artefacts/Release"
vst3="$artefacts/VST3/Schematic Amp Sim.vst3"
exe="$artefacts/Standalone/Schematic Amp Sim"
[ -d "$vst3" ] || { echo "missing bundle: $vst3" >&2; exit 1; }
[ -f "$exe" ] || { echo "missing standalone: $exe" >&2; exit 1; }

echo "Packaging Schematic Amp Sim $full for Linux"
name="SchematicAmpSim-$full-linux-x86_64"
stage="$(mktemp -d)/$name"
mkdir -p "$stage"
cp -R "$vst3" "$stage/"
cp "$exe" "$stage/"
chmod +x "$stage/Schematic Amp Sim"
cp LICENSE README.md "$stage/"
cat > "$stage/INSTALL.txt" <<EOF
Schematic Amp Sim $full for Linux (x86_64)

VST3 plug-in:
  Copy the folder  "Schematic Amp Sim.vst3"  to  ~/.vst3  (for your user) or /usr/lib/vst3 (for everybody), then rescan the plugins in your DAW.

Standalone application:
  Run  ./"Schematic Amp Sim"  (it needs a desktop session and ALSA or JACK).

Both need these libraries (Debian / Ubuntu package names; others are similar):
  libasound2 libx11-6 libxext6 libxrandr2 libxinerama1 libxcursor1 libfreetype6 libfontconfig1 libgl1

This beta was built and tested by GitHub Actions on Ubuntu; it has not been tried in every Linux DAW (see the changelog).
EOF

mkdir -p dist
tarball="dist/$name.tar.gz"
rm -f "$tarball"
tar -C "$(dirname "$stage")" -czf "$tarball" "$name"
echo "  $tarball"
(cd dist && sha256sum "$name.tar.gz" > SHA256SUMS-linux.txt)
echo "Checksums: dist/SHA256SUMS-linux.txt"
