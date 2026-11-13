; SPDX-License-Identifier: GPL-3.0-or-later
; Copyright (C) 2026 Vloom Audio contributors
;
; Windows installer (Inno Setup 6). Built by .github/workflows/release.yml:
;   iscc /DAppVersion=0.2.0 /DNumericVersion=0.2.0 /DStageDir=<windeployqt'd bin folder>
;        /DOutputDir=<dir> VloomDAW.iss
; StageDir holds VloomDAW.exe, vloomdaw-plugin-probe.exe, the Qt DLLs and
; plugins and the MSVC runtime, as windeployqt left them.

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

[Run]
Filename: "{app}\VloomDAW.exe"; Description: "{cm:LaunchProgram,VloomDAW}"; Flags: nowait postinstall skipifsilent
