; Inno Setup script for Instant Record (OBS plugin) — Windows installer.
; Built automatically by GitHub Actions. Installs the plugin into the
; per-user-machine OBS plugins folder so OBS loads it on next launch.

#define MyAppName "Instant Record"
#define MyAppPublisher "Instant Replay"
#define MyAppURL "https://instanrp.com"

#ifndef MyVersion
  #define MyVersion "1.0.0"
#endif
#ifndef MyStage
  #define MyStage "stage"
#endif

[Setup]
AppId={{7C2A9F14-3B6E-4D91-9A2C-1A2B3C4D5E6F}
AppName={#MyAppName}
AppVersion={#MyVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
DefaultDirName={commonappdata}\obs-studio\plugins\instant-record
DisableDirPage=yes
DisableProgramGroupPage=yes
UninstallDisplayName={#MyAppName}
OutputDir=Output
OutputBaseFilename=InstantRecord-Setup
Compression=lzma
SolidCompression=yes
ArchitecturesInstallIn64BitMode=x64
WizardStyle=modern

[Messages]
WelcomeLabel2=This will install [name] into your OBS Studio plugins folder.%n%nPlease CLOSE OBS Studio before continuing.

[Files]
Source: "{#MyStage}\bin\64bit\instant-record.dll"; DestDir: "{app}\bin\64bit"; Flags: ignoreversion
Source: "{#MyStage}\data\*"; DestDir: "{app}\data"; Flags: ignoreversion recursesubdirs createallsubdirs

[UninstallDelete]
Type: filesandordirs; Name: "{app}"

[Code]
function InitializeSetup(): Boolean;
var
  ResultCode: Integer;
begin
  { Warn (don't block) if OBS appears to be running. }
  Exec('cmd.exe', '/C tasklist /FI "IMAGENAME eq obs64.exe" | find /I "obs64.exe"',
       '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
  if ResultCode = 0 then
    MsgBox('OBS Studio looks like it is running. Please close it before installing, then continue.',
           mbInformation, MB_OK);
  Result := True;
end;
