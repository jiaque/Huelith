; Build with ISCC /DPackageDir=<portable folder> /DOutputDir=<output folder>
#ifndef PackageDir
  #error PackageDir must point to a validated portable package
#endif
#ifndef OutputDir
  #error OutputDir must be provided
#endif
#ifndef AppVersion
  #define AppVersion "0.1"
#endif

[Setup]
AppId={{A694576E-1F32-4F76-BA99-951DF82C1541}
AppName=Huelith
AppVersion={#AppVersion}
AppVerName=Huelith v{#AppVersion}
AppPublisher=Huelith
DefaultDirName={localappdata}\Programs\Huelith
DefaultGroupName=Huelith
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
DisableDirPage=no
DisableProgramGroupPage=yes
ShowLanguageDialog=yes
LicenseFile={#PackageDir}\LICENSE
SetupIconFile=..\..\apps\desktop\assets\huelith.ico
UninstallDisplayIcon={app}\Huelith.exe
UninstallDisplayName=Huelith v{#AppVersion}
Uninstallable=yes
CreateUninstallRegKey=yes
OutputDir={#OutputDir}
OutputBaseFilename=Huelith-v{#AppVersion}-Setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes
RestartApplications=no
VersionInfoVersion={#AppVersion}.0.0

[Languages]
Name: "chinesesimp"; MessagesFile: "compiler:Languages\ChineseSimplified.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Registry]
Root: HKCU; Subkey: "Software\Huelith\Huelith\interface"; ValueType: string; ValueName: "language"; ValueData: "{code:InterfaceLanguage}"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#PackageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\Huelith"; Filename: "{app}\Huelith.exe"; WorkingDir: "{app}"
Name: "{group}\{cm:UninstallProgram,Huelith}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\Huelith"; Filename: "{app}\Huelith.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\Huelith.exe"; Description: "{cm:LaunchProgram,Huelith}"; Flags: nowait postinstall skipifsilent

; Deliberately no recursive [UninstallDelete]: user-created files are retained.

[Code]
function InterfaceLanguage(Param: String): String;
begin
  if ActiveLanguage = 'english' then
    Result := 'en'
  else
    Result := 'zh_CN';
end;
