; SPDX-License-Identifier: GPL-3.0-or-later
; Copyright (C) 2026 Vloom Audio contributors
;
; Windows installer (Inno Setup 6). Built by .github/workflows/release.yml:
;   iscc -DAppVersion=0.2.0 -DNumericVersion=0.2.0 -DStageDir=<windeployqt'd bin folder>
;        -DOutputDir=<dir> VloomDAW.iss
; StageDir holds VloomDAW.exe, vloomdaw-plugin-probe.exe, the Qt DLLs and
; plugins (windeployqt) and the MSVC runtime DLLs (app-local).

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef StageDir
  #error StageDir must be given: /DStageDir=<folder with VloomDAW.exe>
#endif
#ifndef NumericVersion
  #define NumericVersion "0.0.0"
#endif
#ifndef OutputDir
  #define OutputDir "."
#endif

[Setup]
; Never change AppId: it is how Windows recognises an installed VloomDAW to
; upgrade or uninstall it.
AppId={{F7F181DB-148C-41FF-8D0C-61250176563F}
AppName=VloomDAW
AppVersion={#AppVersion}
AppVerName=VloomDAW {#AppVersion}
; The file version must be numbers only (AppVersion may read "0.2.0-dev.1a2b3c4").
VersionInfoVersion={#NumericVersion}
AppPublisher=Vloom Audio
AppPublisherURL=https://github.com/vloom-audio/VloomDAW
AppSupportURL=https://github.com/vloom-audio/VloomDAW/issues
DefaultDirName={autopf}\VloomDAW
DefaultGroupName=VloomDAW
DisableProgramGroupPage=yes
LicenseFile=..\..\LICENSE
SetupIconFile=..\..\resources\app\vloomdaw.ico
UninstallDisplayIcon={app}\VloomDAW.exe
OutputDir={#OutputDir}
OutputBaseFilename=VloomDAW-{#AppVersion}-windows-x64-setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
; Per-user or all users, as the person installing chooses.
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
CloseApplications=yes
ChangesAssociations=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\VloomDAW"; Filename: "{app}\VloomDAW.exe"
Name: "{group}\{cm:UninstallProgram,VloomDAW}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\VloomDAW"; Filename: "{app}\VloomDAW.exe"; Tasks: desktopicon

[Registry]
Root: HKA; Subkey: "Software\Classes\.vloom"; ValueType: string; ValueName: ""; ValueData: "VloomDAW.Project"; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Classes\.vloom\OpenWithProgids"; ValueType: string; ValueName: "VloomDAW.Project"; ValueData: ""; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Classes\VloomDAW.Project"; ValueType: string; ValueName: ""; ValueData: "VloomDAW Project"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\VloomDAW.Project\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: """{app}\VloomDAW.exe"",1"
Root: HKA; Subkey: "Software\Classes\VloomDAW.Project\shell\open"; ValueType: string; ValueName: ""; ValueData: "Open with VloomDAW"
Root: HKA; Subkey: "Software\Classes\VloomDAW.Project\shell\open"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\VloomDAW.exe"",0"
Root: HKA; Subkey: "Software\Classes\VloomDAW.Project\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\VloomDAW.exe"" ""%1"""

[Run]
Filename: "{app}\VloomDAW.exe"; Description: "{cm:LaunchProgram,VloomDAW}"; Flags: nowait postinstall skipifsilent
; An update started by VloomDAW itself (ui/update/UpdateInstaller) runs
; silently with /relaunch=1 and starts the new version when it is done.
Filename: "{app}\VloomDAW.exe"; Flags: nowait runasoriginaluser; Check: RelaunchAfterUpdate

[Code]
// VloomDAW passes /waitpid=<its process id> when it updates itself: Setup
// waits (at most two minutes) for it to exit, so no file is in use.
function OpenProcess(Access: Cardinal; Inherit: Boolean; Pid: Cardinal): THandle;
  external 'OpenProcess@kernel32.dll stdcall';
function WaitForSingleObject(Handle: THandle; Milliseconds: Cardinal): Cardinal;
  external 'WaitForSingleObject@kernel32.dll stdcall';
function CloseHandle(Handle: THandle): Boolean;
  external 'CloseHandle@kernel32.dll stdcall';

const
  SYNCHRONIZE = $00100000;

function InitializeSetup(): Boolean;
var
  Pid: Integer;
  Process: THandle;
begin
  Pid := StrToIntDef(ExpandConstant('{param:waitpid|0}'), 0);
  if Pid > 0 then begin
    Process := OpenProcess(SYNCHRONIZE, False, Pid);
    if Process <> 0 then begin
      WaitForSingleObject(Process, 120000);
      CloseHandle(Process);
    end;
  end;
  Result := True;
end;

function RelaunchAfterUpdate(): Boolean;
begin
  Result := WizardSilent() and (ExpandConstant('{param:relaunch|0}') = '1');
end;
