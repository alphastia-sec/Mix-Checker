; Instalator Mix Checker (Windows, VST3) — Inno Setup
[Setup]
AppName=Mix Checker
AppVersion=1.0.0
AppPublisher=Alphastudio
DefaultDirName={commoncf64}\VST3
DisableDirPage=yes
DisableProgramGroupPage=yes
OutputBaseFilename=MixChecker-Setup
OutputDir=Output
ArchitecturesInstallIn64BitMode=x64compatible
ArchitecturesAllowed=x64compatible
PrivilegesRequired=admin
Compression=lzma2
SolidCompression=yes
UninstallDisplayName=Mix Checker (Alphastudio)

[Files]
Source: "..\build\MixChecker_artefacts\Release\VST3\Mix Checker.vst3\*"; DestDir: "{commoncf64}\VST3\Mix Checker.vst3"; Flags: recursesubdirs ignoreversion

[Messages]
SetupWindowTitle=Mix Checker — Alphastudio
