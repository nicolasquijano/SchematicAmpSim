; Inno Setup script for the Windows installer of Schematic Amp Sim.
; Build it with package.cmd (which passes the version), or by hand:
;   ISCC /DAppVersion=0.1.0 /DAppVersionFull=0.1.0-beta installer\schematic-amp-sim.iss
; It installs the VST3 bundle into the system VST3 folder (C:\Program Files\Common Files\VST3), where every DAW looks.

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef AppVersionFull
  #define AppVersionFull AppVersion
#endif

[Setup]
; A fixed id: it is how a newer installer recognises (and replaces) an older install. Never change it.
AppId={{7C9E3B52-1A64-4F0D-9B8E-5D2A6C41E0F7}
AppName=Schematic Amp Sim
AppVersion={#AppVersionFull}
AppVerName=Schematic Amp Sim {#AppVersionFull}
AppPublisher=Nico
AppPublisherURL=https://github.com/nicolasquijano/SchematicAmpSim
AppSupportURL=https://github.com/nicolasquijano/SchematicAmpSim/issues
AppUpdatesURL=https://github.com/nicolasquijano/SchematicAmpSim/releases
VersionInfoVersion={#AppVersion}
DefaultDirName={commoncf64}\VST3\Schematic Amp Sim.vst3
DisableDirPage=auto
DisableProgramGroupPage=yes
LicenseFile=..\LICENSE
OutputDir=..\dist
OutputBaseFilename=SchematicAmpSim-{#AppVersionFull}-win64-setup
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
CloseApplications=yes
RestartApplications=no
UninstallDisplayName=Schematic Amp Sim {#AppVersionFull}

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "spanish"; MessagesFile: "compiler:Languages\Spanish.isl"

[Files]
; The whole bundle: Contents\x86_64-win\Schematic Amp Sim.vst3 (the DLL) and Contents\Resources\moduleinfo.json.
Source: "..\build\plugin\SchematicAmpSim_artefacts\Release\VST3\Schematic Amp Sim.vst3\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion
Source: "..\LICENSE"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\README.md"; DestDir: "{app}"; Flags: ignoreversion

[Messages]
FinishedLabel=Schematic Amp Sim was installed in the VST3 folder. Rescan the plugins in your DAW to see it.
