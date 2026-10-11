#define MyAppName "FT8 Hunter"
#define MyAppVersion "1.0.13 Beta"
#define MyAppPublisher "IU2JMZ"
#define MainExe "wsjtx.exe"

#ifndef StageDir
  #define StageDir "..\..\stage-main"
#endif
#ifndef OmniRigSetup
  #define OmniRigSetup "..\..\vendor\OmniRigSetup.exe"
#endif
#ifndef BetaIcon
  #define BetaIcon "..\branding\FT8Hunter.ico"
#endif

[Setup]
AppId={{8C6C2DF1-CCB7-4E86-9850-1B8F60E7B100}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
VersionInfoVersion=1.0.13.0
DefaultDirName={autopf}\FT8 Hunter Beta
DefaultGroupName=FT8 Hunter Beta
DisableProgramGroupPage=yes
OutputDir=..\..\installer-output-wsjtx
OutputBaseFilename=FT8Hunter_Setup_v1.0.13_Beta
SetupIconFile={#BetaIcon}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
Uninstallable=yes
UninstallDisplayName=FT8 Hunter 1.0.13 Beta
UninstallDisplayIcon={app}\FT8Hunter.ico
SetupLogging=yes

[Languages]
Name: "italian"; MessagesFile: "compiler:Languages\Italian.isl"

[Tasks]
Name: "desktopicon"; Description: "Crea un collegamento sul desktop"; GroupDescription: "Collegamenti:"; Flags: checkedonce
Name: "omnirig"; Description: "Installa OmniRig 1.20 se non è già presente (opzionale)"; GroupDescription: "Componenti opzionali:"; Flags: unchecked

[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#OmniRigSetup}"; DestDir: "{tmp}"; DestName: "OmniRigSetup.exe"; Flags: deleteafterinstall; Tasks: omnirig
Source: "{#BetaIcon}"; DestDir: "{app}"; DestName: "FT8Hunter.ico"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\FT8 Hunter 1.0.13 Beta"; Filename: "{app}\bin\{#MainExe}"; WorkingDir: "{app}\bin"; IconFilename: "{app}\FT8Hunter.ico"
Name: "{autodesktop}\FT8 Hunter 1.0.13 Beta"; Filename: "{app}\bin\{#MainExe}"; WorkingDir: "{app}\bin"; IconFilename: "{app}\FT8Hunter.ico"; Tasks: desktopicon

[Run]
Filename: "{tmp}\OmniRigSetup.exe"; Parameters: "/VERYSILENT /NORESTART"; StatusMsg: "Installazione OmniRig..."; Flags: waituntilterminated; Tasks: omnirig
Filename: "{app}\bin\{#MainExe}"; Description: "Avvia FT8 Hunter 1.0.13 Beta"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
Type: files; Name: "{userappdata}\FT8 Hunter Beta*.ini"; Check: ShouldRemovePersonalData
Type: files; Name: "{localappdata}\FT8 Hunter Beta*.ini"; Check: ShouldRemovePersonalData
Type: filesandordirs; Name: "{userappdata}\FT8 Hunter Beta"; Check: ShouldRemovePersonalData
Type: filesandordirs; Name: "{localappdata}\FT8 Hunter Beta"; Check: ShouldRemovePersonalData

[Code]
var
  RemovePersonalData: Boolean;

function InitializeUninstall(): Boolean;
begin
  RemovePersonalData :=
    MsgBox(
      'Vuoi rimuovere anche le impostazioni e i dati personali di FT8 Hunter Beta?' + #13#10 + #13#10 +
      'Scegli NO per conservare configurazioni, preferenze e dati utente.' + #13#10 +
      'La scelta predefinita e'' NO.',
      mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES;
  Result := True;
end;

function ShouldRemovePersonalData(): Boolean;
begin
  Result := RemovePersonalData;
end;
