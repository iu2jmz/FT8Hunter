#define MyAppName "FT8 Hunter"
#define MyAppVersion "1.0.7"
#define MyAppPublisher "IU2JMZ"
#define MainExe "wsjtx.exe"

#ifndef StageDir
  #define StageDir "..\..\stage-main"
#endif
#ifndef OmniRigSetup
  #define OmniRigSetup "..\..\vendor\OmniRigSetup.exe"
#endif

[Setup]
AppId={{8C6C2DF1-CCB7-4E86-9850-1B8F60E7A100}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={autopf}\FT8 Hunter
DefaultGroupName=FT8 Hunter
DisableProgramGroupPage=yes
OutputDir=..\..\installer-output-wsjtx
OutputBaseFilename=FT8Hunter_Setup_v1.0.7
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
UninstallDisplayName=FT8 Hunter
UninstallDisplayIcon={app}\bin\{#MainExe}
SetupLogging=yes

[Languages]
Name: "italian"; MessagesFile: "compiler:Languages\Italian.isl"

[Tasks]
Name: "desktopicon"; Description: "Crea un collegamento sul desktop"; GroupDescription: "Collegamenti:"; Flags: checkedonce
Name: "omnirig"; Description: "Installa OmniRig 1.20 se non è già presente (opzionale)"; GroupDescription: "Componenti opzionali:"; Flags: unchecked

[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#OmniRigSetup}"; DestDir: "{tmp}"; DestName: "OmniRigSetup.exe"; Flags: deleteafterinstall; Tasks: omnirig

[Icons]
Name: "{autoprograms}\FT8 Hunter"; Filename: "{app}\bin\{#MainExe}"; WorkingDir: "{app}\bin"
Name: "{autodesktop}\FT8 Hunter"; Filename: "{app}\bin\{#MainExe}"; WorkingDir: "{app}\bin"; Tasks: desktopicon

[Run]
Filename: "{tmp}\OmniRigSetup.exe"; Parameters: "/VERYSILENT /NORESTART"; StatusMsg: "Installazione OmniRig..."; Flags: waituntilterminated; Tasks: omnirig
Filename: "{app}\bin\{#MainExe}"; Description: "Avvia FT8 Hunter"; Flags: nowait postinstall skipifsilent
