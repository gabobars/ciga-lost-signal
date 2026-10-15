#define MyAppName "CIGA - Lost Signal"
#define MyAppVersion "0.1.0"
#define MyAppPublisher "CIGA"
#define MyAppId "{{7D74EFC3-CA94-4E11-94C1-3F39EB4A7274}}"
#define PayloadDir "..\release\installer-payload"

[Setup]
AppId={#MyAppId}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppVerName={#MyAppName} {#MyAppVersion}
DefaultDirName={commonappdata}\obs-studio\plugins\ciga-transition
DisableDirPage=yes
DisableProgramGroupPage=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=..\release
OutputBaseFilename=CIGA-Lost-Signal-Setup-v{#MyAppVersion}
UninstallDisplayName={#MyAppName}
UninstallFilesDir={commonappdata}\CIGA\Uninstall\CIGA-Lost-Signal
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
SetupLogging=yes
Uninstallable=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Files]
; OBS Studio 33+ — novo formato
Source: "{#PayloadDir}\ciga-transition.dll"; DestDir: "{commonappdata}\obs-studio\plugins\ciga-transition"; Flags: ignoreversion; Check: UseNewOBSLayout
Source: "{#PayloadDir}\data\*"; DestDir: "{commonappdata}\obs-studio\plugins\ciga-transition\data"; Flags: ignoreversion recursesubdirs createallsubdirs; Check: UseNewOBSLayout

; OBS 32.x e anteriores — atualizar a instalação antiga existente
Source: "{#PayloadDir}\ciga-transition.dll"; DestDir: "{commonpf}\obs-studio\obs-plugins\64bit"; Flags: ignoreversion; Check: UseExistingProgramFilesLayout
Source: "{#PayloadDir}\data\*"; DestDir: "{commonpf}\obs-studio\data\obs-plugins\ciga-transition"; Flags: ignoreversion recursesubdirs createallsubdirs; Check: UseExistingProgramFilesLayout

; OBS 32.x e anteriores — formato ProgramData legado
Source: "{#PayloadDir}\ciga-transition.dll"; DestDir: "{commonappdata}\obs-studio\plugins\ciga-transition\bin\64bit"; Flags: ignoreversion; Check: UseLegacyProgramDataLayout
Source: "{#PayloadDir}\data\*"; DestDir: "{commonappdata}\obs-studio\plugins\ciga-transition\data"; Flags: ignoreversion recursesubdirs createallsubdirs; Check: UseLegacyProgramDataLayout

[UninstallDelete]
Type: dirifempty; Name: "{commonappdata}\obs-studio\plugins\ciga-transition\data"
Type: dirifempty; Name: "{commonappdata}\obs-studio\plugins\ciga-transition\bin\64bit"
Type: dirifempty; Name: "{commonappdata}\obs-studio\plugins\ciga-transition\bin"
Type: dirifempty; Name: "{commonpf}\obs-studio\data\obs-plugins\ciga-transition"

[Code]
function DetectOBSBuildMajor(): Integer;
var
  Major, Minor, Revision, Build: Word;
  OBSExe: String;
begin
  Result := 0;
  OBSExe := ExpandConstant('{pf}\obs-studio\bin\64bit\obs64.exe');

  if FileExists(OBSExe) then
  begin
    if GetVersionComponents(OBSExe, Major, Minor, Revision, Build) then
      Result := Major;
  end;
end;

function UseNewOBSLayout(): Boolean;
var
  Major: Integer;
begin
  Major := DetectOBSBuildMajor();

  if Major >= 33 then
    Result := True
  else if Major > 0 then
    Result := False
  else if FileExists(ExpandConstant('{commonpf}\obs-studio\obs-plugins\64bit\ciga-transition.dll')) then
    Result := False
  else if FileExists(ExpandConstant('{commonappdata}\obs-studio\plugins\ciga-transition\bin\64bit\ciga-transition.dll')) then
    Result := False
  else
    Result := True;
end;

function UseExistingProgramFilesLayout(): Boolean;
begin
  Result := (not UseNewOBSLayout()) and
    FileExists(ExpandConstant('{commonpf}\obs-studio\obs-plugins\64bit\ciga-transition.dll'));
end;

function UseLegacyProgramDataLayout(): Boolean;
begin
  Result := (not UseNewOBSLayout()) and
    (not FileExists(ExpandConstant('{commonpf}\obs-studio\obs-plugins\64bit\ciga-transition.dll')));
end;
function IsProcessRunning(const ImageName: String): Boolean;
var
  ResultCode: Integer;
  CommandLine: String;
begin
  CommandLine := '/C tasklist /FI "IMAGENAME eq ' + ImageName + '" /NH | find /I "' + ImageName + '" > nul';
  Result := False;
  if Exec(ExpandConstant('{sys}\cmd.exe'), CommandLine, '', SW_HIDE, ewWaitUntilTerminated, ResultCode) then
    Result := (ResultCode = 0);
end;

function InitializeSetup(): Boolean;
begin
  if IsProcessRunning('obs64.exe') or IsProcessRunning('obs32.exe') then
  begin
    MsgBox('Fecha completamente o OBS Studio antes de instalar o CIGA - Lost Signal e executa novamente o instalador.', mbInformation, MB_OK);
    Result := False;
  end
  else
    Result := True;
end;






