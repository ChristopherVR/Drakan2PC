; Inno Setup script for DrakanPC-Setup.exe. package_builder_kit.py --installer stages the kit and compiles this with
; ISCC /DStageDir=<staged kit> /DAppVersion=<version> /DOutputDir=<dir> /DOutputName=<file name without .exe>.
; The setup copies the builder (source, bundled tools, third-party sources) and then runs Build-Drakan.cmd with the
; chosen ISO (or /ISO=<path> on the command line); the game itself is built on the player's PC from their own disc.

#ifndef StageDir
  #error Pass /DStageDir=<staged kit directory>
#endif
#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef OutputDir
  #define OutputDir "."
#endif
#ifndef OutputName
  #define OutputName "DrakanPC-Setup"
#endif

[Setup]
AppId={{69932327-BBAB-42A2-AA60-D15FFF9D83E6}
AppName=Drakan: The Ancients' Gates (PC builder)
AppVersion={#AppVersion}
AppVerName=Drakan: The Ancients' Gates PC builder {#AppVersion}
DefaultDirName={localappdata}\DrakanPC
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
OutputDir={#OutputDir}
OutputBaseFilename={#OutputName}
SetupIconFile={#StageDir}\drakan.ico
UninstallDisplayIcon={app}\drakan.ico
InfoBeforeFile={#StageDir}\SETUP-INFO.txt
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
; The build folder holds about 2 GB once the game is built.
ExtraDiskSpaceRequired=2147483648

[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion

[Icons]
Name: "{autoprograms}\Drakan - The Ancients' Gates"; Filename: "{app}\dist\DrakanPC\drakan.exe"; WorkingDir: "{app}\dist\DrakanPC"; IconFilename: "{app}\drakan.ico"
Name: "{autodesktop}\Drakan - The Ancients' Gates"; Filename: "{app}\dist\DrakanPC\drakan.exe"; WorkingDir: "{app}\dist\DrakanPC"; IconFilename: "{app}\drakan.ico"; Tasks: desktopicon
Name: "{autoprograms}\Rebuild Drakan"; Filename: "{app}\Build-Drakan.cmd"; WorkingDir: "{app}"
Name: "{autoprograms}\Drakan play folder"; Filename: "{app}\dist\DrakanPC"

[Tasks]
Name: desktopicon; Description: "Create a desktop shortcut"

[Run]
Filename: "{app}\Build-Drakan.cmd"; Parameters: "-IsoPath ""{code:GetIsoPath}""{code:GetBuildOptions}"; WorkingDir: "{app}"; Description: "Build the game now (one to two hours; installs Microsoft's C++ Build Tools if needed)"; Flags: postinstall shellexec nowait skipifsilent

[UninstallDelete]
; Build products; dist\DrakanPC (saves and settings) is left in place.
Type: filesandordirs; Name: "{app}\build"
Type: files; Name: "{app}\build-log.txt"
; Generated from the disc by the build (scripts/prepare_local_runtime.py).
Type: files; Name: "{app}\ps2recomp\ps2xRuntime\src\lib\vu\aot\ps2_vu1_image.inc"
Type: dirifempty; Name: "{app}\ps2recomp\ps2xRuntime\src\lib\vu\aot"
Type: dirifempty; Name: "{app}\ps2recomp\ps2xRuntime\src\lib\vu"
Type: dirifempty; Name: "{app}\ps2recomp\ps2xRuntime\src\lib"
Type: dirifempty; Name: "{app}\ps2recomp\ps2xRuntime\src"
Type: dirifempty; Name: "{app}\ps2recomp\ps2xRuntime"
Type: dirifempty; Name: "{app}\ps2recomp"

[Code]
var
  IsoPage: TInputFileWizardPage;
  OptionsPage: TInputOptionWizardPage;
  CpuPage: TInputQueryWizardPage;

procedure InitializeWizard;
begin
  IsoPage := CreateInputFilePage(wpSelectDir,
    'Your game disc',
    'Where is your Drakan: The Ancients'' Gates disc image?',
    'Select the .iso file of your own copy of the game: the PAL release (SCES-500.06) or the USA release ' +
    '(SCUS-971.28, experimental: keyboard and mouse work, but not the PC settings, widescreen, quicksave or the ' +
    'modern menus; saves are separate). The game is built from it and reads it ' +
    'while it runs, so keep it where it is afterwards.');
  IsoPage.Add('Disc image (.iso):', 'Disc images (*.iso)|*.iso|All files (*.*)|*.*', '.iso');
  // DrakanPC-Setup.exe /ISO="D:\Games\Drakan.iso" fills it in.
  IsoPage.Values[0] := ExpandConstant('{param:ISO|}');

  OptionsPage := CreateInputOptionPage(IsoPage.ID,
    'Optional extras',
    'Choose what else to build.',
    'NVIDIA DLSS works only on NVIDIA RTX graphics cards. Its SDK is downloaded from NVIDIA during the build, under ' +
    'the NVIDIA RTX SDKs License (https://github.com/NVIDIA/DLSS/blob/v310.9.16/LICENSE.txt); ticking the box means you ' +
    'accept that licence.' + #13#10#13#10 +
    'The browser version needs Windows Subsystem for Linux with Ubuntu (wsl --install -d Ubuntu). You play it on ' +
    'the project''s browser player page by choosing the built folder and your .iso; nothing is uploaded.',
    False, False);
  OptionsPage.Add('NVIDIA DLSS - I accept the NVIDIA RTX SDKs License');
  OptionsPage.Add('Also build the browser version (needs WSL with Ubuntu)');

  CpuPage := CreateInputQueryPage(OptionsPage.ID,
    'Build speed',
    'How much of this PC may the build use?',
    'This PC has ' + GetEnv('NUMBER_OF_PROCESSORS') + ' logical CPU cores. More cores build faster but leave less ' +
    'for other programs; memory still limits how many compilers run at once (each needs up to 6 GB). Leave the ' +
    'field empty to let the builder choose (half the cores, at most 4). These are build settings only: they do not ' +
    'change how the game runs. "Rebuild Drakan" keeps them.');
  CpuPage.Add('CPU cores to use (empty = automatic):', False);
  // DrakanPC-Setup.exe /CORES=8 fills it in.
  CpuPage.Values[0] := ExpandConstant('{param:CORES|}');
  OptionsPage.Add('Keep the PC responsive while building (compile at low priority)');
end;

function NextButtonClick(CurPageID: Integer): Boolean;
var
  Size: Int64;
  Cores: Integer;
begin
  Result := True;
  // A silent install never starts the build (skipifsilent), so it does not need an ISO.
  if (CurPageID = IsoPage.ID) and not (WizardSilent and (IsoPage.Values[0] = '')) then begin
    if not FileExists(IsoPage.Values[0]) then begin
      MsgBox('Choose your disc image (.iso) to continue.', mbError, MB_OK);
      Result := False;
    end else if FileSize64(IsoPage.Values[0], Size) and (Size < 100 * 1024 * 1024) then begin
      MsgBox('That file is too small to be the game disc. Choose the full .iso image.', mbError, MB_OK);
      Result := False;
    end;
  end;
  if (CurPageID = CpuPage.ID) and (Trim(CpuPage.Values[0]) <> '') then begin
    Cores := StrToIntDef(Trim(CpuPage.Values[0]), 0);
    if (Cores < 1) or (Cores > StrToIntDef(GetEnv('NUMBER_OF_PROCESSORS'), 256)) then begin
      MsgBox('Enter a number of cores from 1 to ' + GetEnv('NUMBER_OF_PROCESSORS') + ', or leave the field empty.',
        mbError, MB_OK);
      Result := False;
    end;
  end;
end;

function GetIsoPath(Param: String): String;
begin
  Result := IsoPage.Values[0];
end;

function GetBuildOptions(Param: String): String;
begin
  Result := '';
  if OptionsPage.Values[0] then
    Result := Result + ' -EnableDlss';
  if OptionsPage.Values[1] then
    Result := Result + ' -Web';
  // Always passed, so a reinstall replaces the choice remembered by an earlier one.
  Result := Result + ' -Cores ' + IntToStr(StrToIntDef(Trim(CpuPage.Values[0]), 0));
  if OptionsPage.Values[2] then
    Result := Result + ' -Priority Low'
  else
    Result := Result + ' -Priority Normal';
end;
