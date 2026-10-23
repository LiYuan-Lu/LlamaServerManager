; Inno Setup 6 script for the Windows installer.
; Normally run by build-installer.ps1, which passes:
;   /DAppVersion=x.y.z      version from CMakeLists.txt
;   /DStageDir=<path>       output of `cmake --install` (bin\ with the executables and Qt)
;   /DVcRedist=<path>       vc_redist.x64.exe (optional; omitted = not bundled)
;   /DOutputDir=<path>      where the setup .exe is written

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef StageDir
  #error StageDir must point to the cmake --install output
#endif
#ifndef OutputDir
  #define OutputDir "out"
#endif
#define AppName "Llama Server Manager"
#define AppExe "llama-manager.exe"

[Setup]
; Never change AppId: it identifies the installation for upgrades and uninstalling.
AppId={{3921FBFC-4714-4332-B59C-4B43508818C3}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher=LiYuan-Lu
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
LicenseFile=..\..\LICENSE
SetupIconFile=..\..\apps\gui\icons\app.ico
UninstallDisplayIcon={app}\bin\{#AppExe}
UninstallDisplayName={#AppName}
OutputDir={#OutputDir}
OutputBaseFilename=LlamaServerManager-{#AppVersion}-windows-x64-setup
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
; Program Files and the Visual C++ runtime need administrator rights.
PrivilegesRequired=admin
WizardStyle=modern
Compression=lzma2/max
SolidCompression=yes
; Running copies are closed with "llama-manager.exe --quit" (see [Code]), which also stops the
; Agent and its models cleanly; the Restart Manager would only see the GUI.
CloseApplications=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
#if FileExists(AddBackslash(CompilerPath) + "Languages\ChineseTraditional.isl")
Name: "chinesetraditional"; MessagesFile: "compiler:Languages\ChineseTraditional.isl"
#endif

[CustomMessages]
english.StartAtSignIn=Start {#AppName} when I sign in to Windows
english.InstallingRuntime=Installing the Microsoft Visual C++ runtime...
english.ClosingApp=Closing {#AppName}, its Agent, and running models...
#if FileExists(AddBackslash(CompilerPath) + "Languages\ChineseTraditional.isl")
chinesetraditional.StartAtSignIn=登入 Windows 時啟動 {#AppName}
chinesetraditional.InstallingRuntime=正在安裝 Microsoft Visual C++ 執行階段...
chinesetraditional.ClosingApp=正在關閉 {#AppName}、Agent 與執行中的模型...
#endif

[Tasks]
Name: "startatsignin"; Description: "{cm:StartAtSignIn}"
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
#ifdef VcRedist
Source: "{#VcRedist}"; DestDir: "{tmp}"; DestName: "vc_redist.x64.exe"; Flags: deleteafterinstall; Check: VCRedistNeeded
#endif

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\bin\{#AppExe}"
Name: "{group}\{cm:UninstallProgram,{#AppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\bin\{#AppExe}"; Tasks: desktopicon

[Run]
#ifdef VcRedist
Filename: "{tmp}\vc_redist.x64.exe"; Parameters: "/install /quiet /norestart"; StatusMsg: "{cm:InstallingRuntime}"; Check: VCRedistNeeded; Flags: waituntilterminated
#endif
; The sign-in entry belongs to the user who ran the installer, not to the elevated account.
Filename: "{app}\bin\{#AppExe}"; Parameters: "--set-autostart on"; Tasks: startatsignin; Flags: runasoriginaluser runhidden waituntilterminated
Filename: "{app}\bin\{#AppExe}"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: runasoriginaluser nowait postinstall skipifsilent

[UninstallRun]
Filename: "{app}\bin\{#AppExe}"; Parameters: "--set-autostart off"; RunOnceId: "RemoveSignIn"; Flags: runhidden waituntilterminated

; Profiles, settings, and logs in %APPDATA%\LlamaServerManager are kept on purpose.

[Code]
// Inno Setup's check lists (the Tasks page and the "Launch" option on the last page) reserve
// room for a 13 px check box at any DPI but draw the themed check box at its DPI-scaled size,
// right-aligned to that room. Above 100% display scaling the box then slides under the left
// edge and looks clipped. Widening Offset by the extra width gives the box its room back.
procedure FixCheckListBox(List: TNewCheckListBox);
var
  Extra: Integer;
begin
  Extra := ScaleX(13) - 13;
  if Extra > 0 then
    List.Offset := List.Offset + Extra;
end;

procedure InitializeWizard();
begin
  FixCheckListBox(WizardForm.TasksList);
  FixCheckListBox(WizardForm.RunList);
end;

function VCRedistNeeded(): Boolean;
var
  Installed: Cardinal;
begin
  Result := not (RegQueryDWordValue(HKLM64, 'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64', 'Installed', Installed) and (Installed = 1));
end;

// Asks a running copy to close its window and stop the Agent and models, so files can be replaced.
procedure QuitRunningCopy(const Exe: String; AsOriginalUser: Boolean);
var
  ResultCode: Integer;
begin
  if not FileExists(Exe) then
    Exit;
  if AsOriginalUser then
    ExecAsOriginalUser(Exe, '--quit', '', SW_HIDE, ewWaitUntilTerminated, ResultCode)
  else
    Exec(Exe, '--quit', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  WizardForm.PreparingLabel.Caption := CustomMessage('ClosingApp');
  QuitRunningCopy(AddBackslash(WizardDirValue) + 'bin\{#AppExe}', True);
  Result := '';
end;

function InitializeUninstall(): Boolean;
begin
  QuitRunningCopy(ExpandConstant('{app}\bin\{#AppExe}'), False);
  Result := True;
end;
