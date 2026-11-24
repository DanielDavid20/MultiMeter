; Instalador de MultiMeter (VST3, Windows x64)
[Setup]
AppName=MultiMeter
AppVersion=1.0.0
AppPublisher=VMP
CreateAppDir=no
DisableDirPage=yes
DisableProgramGroupPage=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=output
OutputBaseFilename=MultiMeter-Setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
DefaultGroupName=MultiMeter
UninstallDisplayName=MultiMeter VST3

[Files]
Source: "..\build\MultiMeter_artefacts\Release\VST3\MultiMeter.vst3\*"; DestDir: "{commoncf64}\VST3\MultiMeter.vst3"; Flags: recursesubdirs createallsubdirs ignoreversion

[Messages]
FinishedLabel=MultiMeter se instaló en C:\Program Files\Common Files\VST3.%nEn FL Studio: Options > Manage plugins > Find plugins (o "Verify plugins") y búscalo como "MultiMeter".
