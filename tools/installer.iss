; MfPlayer Inno Setup installer script
; Prerequisites: run tools\package.ps1 first to create deploy\MfPlayer\
; Then compile this with Inno Setup Compiler (ISCC.exe)

#define MyAppName "MfPlayer"
#define MyAppVersion "1.0"
#define MyAppPublisher "abcming"
#define MyAppURL "https://github.com/abcming/MfPlayer"
#define MyAppExeName "MfPlayer.exe"

[Setup]
AppId={{B8F4A3D2-7E16-4C82-9D5A-F1E38C02B9A6}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
AllowNoIcons=yes
OutputDir=..\deploy
OutputBaseFilename=MfPlayer-{#MyAppVersion}-setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
SetupIconFile=..\resources\appicon.ico
UninstallDisplayIcon={app}\{#MyAppExeName}
UninstallDisplayName={#MyAppName}
VersionInfoVersion={#MyAppVersion}
VersionInfoCompany={#MyAppPublisher}

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: checkedonce
Name: "fileassoc\mp4";  Description: ".mp4  (MP4)";  GroupDescription: "Register as default player for:"; Flags: checkedonce
Name: "fileassoc\mkv";  Description: ".mkv  (MKV)";  GroupDescription: "Register as default player for:"; Flags: checkedonce
Name: "fileassoc\avi";  Description: ".avi  (AVI)";  GroupDescription: "Register as default player for:"
Name: "fileassoc\mov";  Description: ".mov  (QuickTime)"; GroupDescription: "Register as default player for:"
Name: "fileassoc\wmv";  Description: ".wmv  (WMV)"; GroupDescription: "Register as default player for:"
Name: "fileassoc\webm"; Description: ".webm (WebM)"; GroupDescription: "Register as default player for:"
Name: "fileassoc\flv";  Description: ".flv  (Flash)"; GroupDescription: "Register as default player for:"
Name: "fileassoc\m4v";  Description: ".m4v  (M4V)"; GroupDescription: "Register as default player for:"
Name: "fileassoc\ts";   Description: ".ts   (MPEG-TS)"; GroupDescription: "Register as default player for:"
Name: "fileassoc\m2ts"; Description: ".m2ts (Blu-ray)"; GroupDescription: "Register as default player for:"
Name: "fileassoc\ogv";  Description: ".ogv  (Ogg)"; GroupDescription: "Register as default player for:"

[Files]
Source: "..\deploy\MfPlayer\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Registry]
; .mp4
Root: HKA; Subkey: "Software\Classes\.mp4\OpenWithProgids"; ValueType: string; ValueName: "MfPlayer.mp4"; ValueData: ""; Flags: uninsdeletevalue; Tasks: fileassoc\mp4
Root: HKA; Subkey: "Software\Classes\MfPlayer.mp4"; ValueType: string; ValueData: "MP4 Video"; Flags: uninsdeletekey; Tasks: fileassoc\mp4
Root: HKA; Subkey: "Software\Classes\MfPlayer.mp4\DefaultIcon"; ValueType: string; ValueData: "{app}\{#MyAppExeName},0"; Tasks: fileassoc\mp4
Root: HKA; Subkey: "Software\Classes\MfPlayer.mp4\shell\open\command"; ValueType: string; ValueData: """{app}\{#MyAppExeName}"" ""%1"""; Tasks: fileassoc\mp4
; .mkv
Root: HKA; Subkey: "Software\Classes\.mkv\OpenWithProgids"; ValueType: string; ValueName: "MfPlayer.mkv"; ValueData: ""; Flags: uninsdeletevalue; Tasks: fileassoc\mkv
Root: HKA; Subkey: "Software\Classes\MfPlayer.mkv"; ValueType: string; ValueData: "MKV Video"; Flags: uninsdeletekey; Tasks: fileassoc\mkv
Root: HKA; Subkey: "Software\Classes\MfPlayer.mkv\DefaultIcon"; ValueType: string; ValueData: "{app}\{#MyAppExeName},0"; Tasks: fileassoc\mkv
Root: HKA; Subkey: "Software\Classes\MfPlayer.mkv\shell\open\command"; ValueType: string; ValueData: """{app}\{#MyAppExeName}"" ""%1"""; Tasks: fileassoc\mkv
; .avi
Root: HKA; Subkey: "Software\Classes\.avi\OpenWithProgids"; ValueType: string; ValueName: "MfPlayer.avi"; ValueData: ""; Flags: uninsdeletevalue; Tasks: fileassoc\avi
Root: HKA; Subkey: "Software\Classes\MfPlayer.avi"; ValueType: string; ValueData: "AVI Video"; Flags: uninsdeletekey; Tasks: fileassoc\avi
Root: HKA; Subkey: "Software\Classes\MfPlayer.avi\DefaultIcon"; ValueType: string; ValueData: "{app}\{#MyAppExeName},0"; Tasks: fileassoc\avi
Root: HKA; Subkey: "Software\Classes\MfPlayer.avi\shell\open\command"; ValueType: string; ValueData: """{app}\{#MyAppExeName}"" ""%1"""; Tasks: fileassoc\avi
; .mov
Root: HKA; Subkey: "Software\Classes\.mov\OpenWithProgids"; ValueType: string; ValueName: "MfPlayer.mov"; ValueData: ""; Flags: uninsdeletevalue; Tasks: fileassoc\mov
Root: HKA; Subkey: "Software\Classes\MfPlayer.mov"; ValueType: string; ValueData: "QuickTime Video"; Flags: uninsdeletekey; Tasks: fileassoc\mov
Root: HKA; Subkey: "Software\Classes\MfPlayer.mov\DefaultIcon"; ValueType: string; ValueData: "{app}\{#MyAppExeName},0"; Tasks: fileassoc\mov
Root: HKA; Subkey: "Software\Classes\MfPlayer.mov\shell\open\command"; ValueType: string; ValueData: """{app}\{#MyAppExeName}"" ""%1"""; Tasks: fileassoc\mov
; .wmv
Root: HKA; Subkey: "Software\Classes\.wmv\OpenWithProgids"; ValueType: string; ValueName: "MfPlayer.wmv"; ValueData: ""; Flags: uninsdeletevalue; Tasks: fileassoc\wmv
Root: HKA; Subkey: "Software\Classes\MfPlayer.wmv"; ValueType: string; ValueData: "Windows Media Video"; Flags: uninsdeletekey; Tasks: fileassoc\wmv
Root: HKA; Subkey: "Software\Classes\MfPlayer.wmv\DefaultIcon"; ValueType: string; ValueData: "{app}\{#MyAppExeName},0"; Tasks: fileassoc\wmv
Root: HKA; Subkey: "Software\Classes\MfPlayer.wmv\shell\open\command"; ValueType: string; ValueData: """{app}\{#MyAppExeName}"" ""%1"""; Tasks: fileassoc\wmv
; .webm
Root: HKA; Subkey: "Software\Classes\.webm\OpenWithProgids"; ValueType: string; ValueName: "MfPlayer.webm"; ValueData: ""; Flags: uninsdeletevalue; Tasks: fileassoc\webm
Root: HKA; Subkey: "Software\Classes\MfPlayer.webm"; ValueType: string; ValueData: "WebM Video"; Flags: uninsdeletekey; Tasks: fileassoc\webm
Root: HKA; Subkey: "Software\Classes\MfPlayer.webm\DefaultIcon"; ValueType: string; ValueData: "{app}\{#MyAppExeName},0"; Tasks: fileassoc\webm
Root: HKA; Subkey: "Software\Classes\MfPlayer.webm\shell\open\command"; ValueType: string; ValueData: """{app}\{#MyAppExeName}"" ""%1"""; Tasks: fileassoc\webm
; .flv
Root: HKA; Subkey: "Software\Classes\.flv\OpenWithProgids"; ValueType: string; ValueName: "MfPlayer.flv"; ValueData: ""; Flags: uninsdeletevalue; Tasks: fileassoc\flv
Root: HKA; Subkey: "Software\Classes\MfPlayer.flv"; ValueType: string; ValueData: "Flash Video"; Flags: uninsdeletekey; Tasks: fileassoc\flv
Root: HKA; Subkey: "Software\Classes\MfPlayer.flv\DefaultIcon"; ValueType: string; ValueData: "{app}\{#MyAppExeName},0"; Tasks: fileassoc\flv
Root: HKA; Subkey: "Software\Classes\MfPlayer.flv\shell\open\command"; ValueType: string; ValueData: """{app}\{#MyAppExeName}"" ""%1"""; Tasks: fileassoc\flv
; .m4v
Root: HKA; Subkey: "Software\Classes\.m4v\OpenWithProgids"; ValueType: string; ValueName: "MfPlayer.m4v"; ValueData: ""; Flags: uninsdeletevalue; Tasks: fileassoc\m4v
Root: HKA; Subkey: "Software\Classes\MfPlayer.m4v"; ValueType: string; ValueData: "M4V Video"; Flags: uninsdeletekey; Tasks: fileassoc\m4v
Root: HKA; Subkey: "Software\Classes\MfPlayer.m4v\DefaultIcon"; ValueType: string; ValueData: "{app}\{#MyAppExeName},0"; Tasks: fileassoc\m4v
Root: HKA; Subkey: "Software\Classes\MfPlayer.m4v\shell\open\command"; ValueType: string; ValueData: """{app}\{#MyAppExeName}"" ""%1"""; Tasks: fileassoc\m4v
; .ts
Root: HKA; Subkey: "Software\Classes\.ts\OpenWithProgids"; ValueType: string; ValueName: "MfPlayer.ts"; ValueData: ""; Flags: uninsdeletevalue; Tasks: fileassoc\ts
Root: HKA; Subkey: "Software\Classes\MfPlayer.ts"; ValueType: string; ValueData: "MPEG Transport Stream"; Flags: uninsdeletekey; Tasks: fileassoc\ts
Root: HKA; Subkey: "Software\Classes\MfPlayer.ts\DefaultIcon"; ValueType: string; ValueData: "{app}\{#MyAppExeName},0"; Tasks: fileassoc\ts
Root: HKA; Subkey: "Software\Classes\MfPlayer.ts\shell\open\command"; ValueType: string; ValueData: """{app}\{#MyAppExeName}"" ""%1"""; Tasks: fileassoc\ts
; .m2ts
Root: HKA; Subkey: "Software\Classes\.m2ts\OpenWithProgids"; ValueType: string; ValueName: "MfPlayer.m2ts"; ValueData: ""; Flags: uninsdeletevalue; Tasks: fileassoc\m2ts
Root: HKA; Subkey: "Software\Classes\MfPlayer.m2ts"; ValueType: string; ValueData: "Blu-ray Video"; Flags: uninsdeletekey; Tasks: fileassoc\m2ts
Root: HKA; Subkey: "Software\Classes\MfPlayer.m2ts\DefaultIcon"; ValueType: string; ValueData: "{app}\{#MyAppExeName},0"; Tasks: fileassoc\m2ts
Root: HKA; Subkey: "Software\Classes\MfPlayer.m2ts\shell\open\command"; ValueType: string; ValueData: """{app}\{#MyAppExeName}"" ""%1"""; Tasks: fileassoc\m2ts
; .ogv
Root: HKA; Subkey: "Software\Classes\.ogv\OpenWithProgids"; ValueType: string; ValueName: "MfPlayer.ogv"; ValueData: ""; Flags: uninsdeletevalue; Tasks: fileassoc\ogv
Root: HKA; Subkey: "Software\Classes\MfPlayer.ogv"; ValueType: string; ValueData: "Ogg Video"; Flags: uninsdeletekey; Tasks: fileassoc\ogv
Root: HKA; Subkey: "Software\Classes\MfPlayer.ogv\DefaultIcon"; ValueType: string; ValueData: "{app}\{#MyAppExeName},0"; Tasks: fileassoc\ogv
Root: HKA; Subkey: "Software\Classes\MfPlayer.ogv\shell\open\command"; ValueType: string; ValueData: """{app}\{#MyAppExeName}"" ""%1"""; Tasks: fileassoc\ogv

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#MyAppName}}"; Flags: nowait postinstall skipifsilent
