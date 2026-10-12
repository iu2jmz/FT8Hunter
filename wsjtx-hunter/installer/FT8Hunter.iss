#define MyAppName "FT8 Hunter"
#define MyAppVersion "1.0.14"
#define MyAppPublisher "IU2JMZ"
#define MainExe "wsjtx.exe"

#ifndef StageDir
  #define StageDir "..\..\stage-main"
#endif
#ifndef OmniRigSetup
  #define OmniRigSetup "..\..\vendor\OmniRigSetup.exe"
#endif
#ifndef AppIcon
  #define AppIcon "..\branding\FT8Hunter.ico"
#endif

[Setup]
; Stesso AppId della linea stabile 1.0.12/1.0.13: la 1.0.14 e' un upgrade.
AppId={{8C6C2DF1-CCB7-4E86-9850-1B8F60E7A100}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
VersionInfoVersion=1.0.14.0
DefaultDirName={autopf}\FT8 Hunter
DefaultGroupName=FT8 Hunter
DisableProgramGroupPage=yes
OutputDir=..\..\installer-output-wsjtx
OutputBaseFilename=FT8Hunter_Setup_v1.0.14
SetupIconFile={#AppIcon}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
Uninstallable=yes
UninstallDisplayName=FT8 Hunter 1.0.14
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
Source: "{#AppIcon}"; DestDir: "{app}"; DestName: "FT8Hunter.ico"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\FT8 Hunter"; Filename: "{app}\bin\{#MainExe}"; WorkingDir: "{app}\bin"; IconFilename: "{app}\FT8Hunter.ico"
Name: "{autodesktop}\FT8 Hunter"; Filename: "{app}\bin\{#MainExe}"; WorkingDir: "{app}\bin"; IconFilename: "{app}\FT8Hunter.ico"; Tasks: desktopicon

[Run]
Filename: "{tmp}\OmniRigSetup.exe"; Parameters: "/VERYSILENT /NORESTART"; StatusMsg: "Installazione OmniRig..."; Flags: waituntilterminated; Tasks: omnirig
Filename: "{app}\bin\{#MainExe}"; Description: "Avvia FT8 Hunter 1.0.14"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
; Solo se l'utente sceglie esplicitamente di rimuovere anche i dati personali.
Type: files; Name: "{userappdata}\FT8 Hunter.ini"; Check: ShouldRemovePersonalData
Type: files; Name: "{userappdata}\FT8 Hunter - *.ini"; Check: ShouldRemovePersonalData
Type: files; Name: "{localappdata}\FT8 Hunter.ini"; Check: ShouldRemovePersonalData
Type: files; Name: "{localappdata}\FT8 Hunter - *.ini"; Check: ShouldRemovePersonalData
Type: filesandordirs; Name: "{userappdata}\FT8 Hunter"; Check: ShouldRemovePersonalData
Type: filesandordirs; Name: "{localappdata}\FT8 Hunter"; Check: ShouldRemovePersonalData

[Code]
var
  RemovePersonalData: Boolean;

function InitializeUninstall(): Boolean;
begin
  RemovePersonalData :=
    MsgBox(
      'Vuoi rimuovere anche le impostazioni e i dati personali di FT8 Hunter?' + #13#10 + #13#10 +
      'Scegli NO per conservare configurazione radio/audio, Log4OM/MySQL, preferenze, blacklist e dati utente.' + #13#10 +
      'La scelta predefinita e'' NO.',
      mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES;
  Result := True;
end;

function ShouldRemovePersonalData(): Boolean;
begin
  Result := RemovePersonalData;
end;
