; BluePrinter installer script (Inno Setup 6.3+)
;
; Build from the repo root with:
;   "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" installer\BluePrinter.iss
; Output: build\installer\BluePrinterSetup-<version>.exe
;
; Before packaging, make sure the Release build AND the WebUI build are
; current:
;   npm run build                  (WebUI)
;   cmake --build build --config Release --target BluePrinter_Standalone BluePrinter_VST3
;
; Notes:
; - Per-machine install (admin). The VST3 goes to {commoncf}\VST3, which
;   every DAW scans by default - no registry entries needed.
; - VC++ redistributable and the WebView2 Runtime are downloaded at
;   install time only when missing (the app is a WebView2 host and links
;   the dynamic MSVC runtime, /MD).
; - Uninstall intentionally does NOT touch %APPDATA%\Retrokielto or the
;   snippet library folder - those are user data.

#define MyAppName "BluePrinter"
#ifndef MyAppVersion
#define MyAppVersion "1.0.0"
#endif
#define MyAppPublisher "Retrokielto"
#define MyAppExeName "BluePrinter.exe"

[Setup]
AppId={{E4B8F5C1-9D3E-4A2B-8F67-1C2D3E4F5A6B}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={autopf}\BluePrinter
DefaultGroupName=BluePrinter
UninstallDisplayIcon={app}\{#MyAppExeName}
OutputDir={#SourcePath}..\build\installer
OutputBaseFilename=BluePrinterSetup-{#MyAppVersion}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
MinVersion=10.0.14393
LicenseFile={#SourcePath}..\LICENSE
; Close the running standalone so the .exe can be replaced. If a DAW has
; BluePrinter.vst3 loaded, Inno will prompt (Restart Manager).
CloseApplications=yes
CloseApplicationsFilter=BluePrinter.exe

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop shortcut"; GroupDescription: "Additional icons:"; Flags: unchecked

[Files]
; VST3 bundle -> the folder every DAW scans.
Source: "{#SourcePath}..\build\BluePrinter_artefacts\Release\VST3\BluePrinter.vst3"; DestDir: "{commoncf}\VST3"; Flags: ignoreversion recursesubdirs createallsubdirs
; Standalone + WebUI must stay side by side: WebViewEditor walks up from
; the exe looking for WebUI/dist/index.html.
Source: "{#SourcePath}..\build\BluePrinter_artefacts\Release\Standalone\BluePrinter.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourcePath}..\WebUI\dist\*"; DestDir: "{app}\WebUI\dist"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\BluePrinter"; Filename: "{app}\{#MyAppExeName}"
Name: "{autodesktop}\BluePrinter"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
; Only run these when PrepareToInstall had to download them (Check both).
Filename: "{tmp}\vc_redist.x64.exe"; Parameters: "/install /quiet /norestart"; StatusMsg: "Installing Microsoft VC++ Redistributable..."; Flags: skipifdoesntexist; Check: not IsVCRuntimeInstalled
Filename: "{tmp}\WebView2Setup.exe"; Parameters: "/silent /install"; StatusMsg: "Installing WebView2 Runtime..."; Flags: skipifdoesntexist; Check: not IsWebView2Installed
Filename: "{app}\{#MyAppExeName}"; Description: "Launch {#MyAppName}"; Flags: nowait postinstall skipifsilent

[Code]
const
  VCRedistUrl = 'https://aka.ms/vs/17/release/vc_redist.x64.exe';
  WebView2Url = 'https://go.microsoft.com/fwlink/p/?LinkId=2124703';

function IsVCRuntimeInstalled(): Boolean;
var
  V: Cardinal;
begin
  Result := RegQueryDWordValue(HKLM64, 'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64', 'Installed', V) and (V = 1);
end;

function IsWebView2Installed(): Boolean;
begin
  Result := RegKeyExists(HKLM64, 'SOFTWARE\Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}') or
            RegKeyExists(HKLM32, 'SOFTWARE\Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}');
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  if not IsVCRuntimeInstalled() then
  begin
    try
      DownloadTemporaryFile(VCRedistUrl, 'vc_redist.x64.exe', '', nil);
    except
      Result := 'Could not download the VC++ Redistributable (https://aka.ms/vs/17/release/vc_redist.x64.exe). Please install it manually and rerun the installer.';
      exit;
    end;
  end;

  if not IsWebView2Installed() then
  begin
    try
      DownloadTemporaryFile(WebView2Url, 'WebView2Setup.exe', '', nil);
    except
      Result := 'Could not download the WebView2 Runtime. Please install it from https://developer.microsoft.com/microsoft-edge/webview2 and rerun the installer.';
      exit;
    end;
  end;

  Result := '';
end;
