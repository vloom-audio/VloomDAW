; SPDX-License-Identifier: GPL-3.0-or-later
; Copyright (C) 2026 Vloom Audio contributors
;
; Windows installer (Inno Setup 6). Built by .github/workflows/release.yml:
;   iscc -DAppVersion=0.2.0 -DNumericVersion=0.2.0 -DStageDir=<stage folder>
;        -DOutputDir=<dir> VloomDAW.iss
; StageDir is installed as it is, so {app} gets the same layout:
;   bin\       VloomDAW.exe, its helper programs (vloomdaw-plugin-probe.exe,
;              vloomdaw-stems.exe, vloom-plugin-bridge32.exe), every DLL (Qt,
;              FFmpeg, ONNX Runtime, DirectML, the app-local MSVC runtime),
;              qt.conf and the Qt plugins in bin\plugins (windeployqt)
;   licenses\  the licences of what ships with it

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef StageDir
  #error StageDir must be given: /DStageDir=<folder with bin\VloomDAW.exe>
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
UninstallDisplayIcon={app}\bin\VloomDAW.exe
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

[InstallDelete]
; VloomDAW has never shipped the older flat layout (everything in {app});
; this only cleans a test install of it, and only when {app}\VloomDAW.exe
; shows that layout is there. Only {app} itself: these wildcards do not
; reach into bin\ or licenses\.
Type: files; Name: "{app}\VloomDAW.exe"; Check: OldFlatLayout
Type: files; Name: "{app}\vloomdaw-plugin-probe.exe"; Check: OldFlatLayout
Type: files; Name: "{app}\vloomdaw-stems.exe"; Check: OldFlatLayout
Type: files; Name: "{app}\vloom-plugin-bridge32.exe"; Check: OldFlatLayout
Type: files; Name: "{app}\*.dll"; Check: OldFlatLayout
Type: files; Name: "{app}\*-LICENSE.txt"; Check: OldFlatLayout
Type: files; Name: "{app}\OnnxRuntime-ThirdPartyNotices.txt"; Check: OldFlatLayout
Type: filesandordirs; Name: "{app}\platforms"; Check: OldFlatLayout
Type: filesandordirs; Name: "{app}\styles"; Check: OldFlatLayout
Type: filesandordirs; Name: "{app}\imageformats"; Check: OldFlatLayout
Type: filesandordirs; Name: "{app}\iconengines"; Check: OldFlatLayout
Type: filesandordirs; Name: "{app}\tls"; Check: OldFlatLayout
Type: filesandordirs; Name: "{app}\networkinformation"; Check: OldFlatLayout
Type: filesandordirs; Name: "{app}\generic"; Check: OldFlatLayout

[Icons]
Name: "{group}\VloomDAW"; Filename: "{app}\bin\VloomDAW.exe"
Name: "{group}\{cm:UninstallProgram,VloomDAW}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\VloomDAW"; Filename: "{app}\bin\VloomDAW.exe"; Tasks: desktopicon

[Registry]
Root: HKA; Subkey: "Software\Classes\.vloom"; ValueType: string; ValueName: ""; ValueData: "VloomDAW.Project"; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Classes\.vloom\OpenWithProgids"; ValueType: string; ValueName: "VloomDAW.Project"; ValueData: ""; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Classes\VloomDAW.Project"; ValueType: string; ValueName: ""; ValueData: "VloomDAW Project"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\VloomDAW.Project\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: """{app}\bin\VloomDAW.exe"",1"
Root: HKA; Subkey: "Software\Classes\VloomDAW.Project\shell\open"; ValueType: string; ValueName: ""; ValueData: "Open with VloomDAW"
Root: HKA; Subkey: "Software\Classes\VloomDAW.Project\shell\open"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\bin\VloomDAW.exe"",0"
Root: HKA; Subkey: "Software\Classes\VloomDAW.Project\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\bin\VloomDAW.exe"" ""%1"""

[Run]
Filename: "{app}\bin\VloomDAW.exe"; Description: "{cm:LaunchProgram,VloomDAW}"; Flags: nowait postinstall skipifsilent
; An update started by VloomDAW itself (ui/update/UpdateInstaller) runs
; silently with /relaunch=1 and starts the new version when it is done.
Filename: "{app}\bin\VloomDAW.exe"; Flags: nowait runasoriginaluser; Check: RelaunchAfterUpdate

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

var
  FlatLayoutChecked, FlatLayoutFound: Boolean;

// Decided once, before [InstallDelete] removes the first file it looks at.
function OldFlatLayout(): Boolean;
begin
  if not FlatLayoutChecked then begin
    FlatLayoutFound := FileExists(ExpandConstant('{app}\VloomDAW.exe'));
    FlatLayoutChecked := True;
  end;
  Result := FlatLayoutFound;
end;

function RelaunchAfterUpdate(): Boolean;
begin
  Result := WizardSilent() and (ExpandConstant('{param:relaunch|0}') = '1');
end;
