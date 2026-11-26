; Gorgon 2D Editor — One-Click Windows Installer (Inno Setup 6)
; Installs per-user (no admin/UAC required) with Editor, Player, Vulkan loader, and DemoScene.

#ifndef AppVersion
  #define AppVersion "4.2.0"
#endif
#ifndef BinDir
  #define BinDir "..\..\..\build\WindowsTesting\Bin\Release"
#endif
#ifndef RepoRoot
  #define RepoRoot "..\..\.."
#endif
#ifndef OutputDir
  #define OutputDir "..\..\..\build\WindowsTesting\Installer"
#endif

#define AppName "Gorgon 2D Editor"
#define AppPublisher "Gorgon Library"
#define AppURL "https://github.com/Efendiminati/Gorgon"
#define AppExeName "GorgonEditor.exe"

[Setup]
AppId={{7E8C4B2A-6D1F-4A9E-9C3B-2F1A8E5D4C6B}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppURL}
AppSupportURL={#AppURL}
AppUpdatesURL={#AppURL}
DefaultDirName={autopf}\Gorgon 2D Editor
DefaultGroupName={#AppName}
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
; True one-click flow: skip redundant wizard pages, install immediately.
DisableWelcomePage=yes
DisableDirPage=yes
DisableProgramGroupPage=yes
DisableReadyPage=yes
OutputDir={#OutputDir}
OutputBaseFilename=GorgonEditor-Setup-{#AppVersion}-x64
SetupIconFile=gorgon-editor.ico
UninstallDisplayIcon={app}\gorgon-editor.ico
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
CloseApplications=force
RestartApplications=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "turkish"; MessagesFile: "compiler:Languages\Turkish.isl"

[Files]
Source: "{#BinDir}\GorgonEditor.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#BinDir}\GorgonPlayer.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#BinDir}\vulkan-1.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "gorgon-editor.ico"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#RepoRoot}\Tools\Editor\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#RepoRoot}\Examples\EditorDemo\Scene\*"; DestDir: "{app}\DemoScene"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\{#AppName}\{#AppName}"; Filename: "{app}\{#AppExeName}"; WorkingDir: "{app}"; IconFilename: "{app}\gorgon-editor.ico"
Name: "{autoprograms}\{#AppName}\Open Demo Scene"; Filename: "{app}\{#AppExeName}"; Parameters: """{app}\DemoScene\DemoScene.gscene.json"""; WorkingDir: "{app}\DemoScene"; IconFilename: "{app}\gorgon-editor.ico"
Name: "{autoprograms}\{#AppName}\Uninstall {#AppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExeName}"; WorkingDir: "{app}"; IconFilename: "{app}\gorgon-editor.ico"

[Registry]
; Add non-intrusive right-click context menu for .json scene files (does not hijack default .json handler)
Root: HKCU; Subkey: "Software\Classes\SystemFileAssociations\.json\shell\OpenWithGorgonEditor"; ValueType: string; ValueName: ""; ValueData: "Open in Gorgon 2D Editor"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\SystemFileAssociations\.json\shell\OpenWithGorgonEditor"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\gorgon-editor.ico"""
Root: HKCU; Subkey: "Software\Classes\SystemFileAssociations\.json\shell\OpenWithGorgonEditor\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#AppExeName}"" ""%1"""

[Run]
Filename: "{app}\{#AppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(AppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent
