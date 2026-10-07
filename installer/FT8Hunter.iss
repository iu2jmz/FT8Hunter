#define MyAppName "FT8 Hunter"
#define MyAppVersion "0.6.1"
#define MyAppPublisher "IU2JMZ"
#define MyAppExeName "FT8Hunter_Test.exe"

[Setup]
AppId={{A7B5D1A4-9D86-4B9E-BB35-4A3A8F5C7300}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={autopf}\FT8 Hunter
DefaultGroupName=FT8 Hunter
DisableProgramGroupPage=yes
OutputDir=..\installer-output
OutputBaseFilename=FT8Hunter_Setup_v0.6.1
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
UninstallDisplayName=FT8 Hunter
UninstallDisplayIcon={app}\{#MyAppExeName}
SetupLogging=yes

[Languages]
Name: "italian"; MessagesFile: "compiler:Languages\Italian.isl"

[Tasks]
Name: "desktopicon"; Description: "Crea un collegamento sul desktop"; GroupDescription: "Collegamenti:"; Flags: checkedonce
Name: "omnirig"; Description: "Installa OmniRig 1.20 (necessario per il controllo CAT della radio)"; GroupDescription: "Componenti opzionali:"; Flags: unchecked

[Files]
Source: "..\publish\win-x64\{#MyAppExeName}"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\vendor\OmniRigSetup.exe"; DestDir: "{tmp}"; Flags: deleteafterinstall; Tasks: omnirig

[Icons]
Name: "{autoprograms}\FT8 Hunter"; Filename: "{app}\{#MyAppExeName}"
Name: "{autodesktop}\FT8 Hunter"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{tmp}\OmniRigSetup.exe"; Parameters: "/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /SP-"; StatusMsg: "Installazione di OmniRig..."; Flags: waituntilterminated; Tasks: omnirig
Filename: "{app}\{#MyAppExeName}"; Description: "Avvia FT8 Hunter"; Flags: nowait postinstall skipifsilent

[Code]
function OmniRigInstalled(): Boolean;
var
  Dummy: String;
begin
  Result := RegQueryStringValue(HKCR, 'OmniRig.OmniRigX\CLSID', '', Dummy);
end;

procedure InitializeWizard();
begin
  { OmniRig resta facoltativo. }
end;
