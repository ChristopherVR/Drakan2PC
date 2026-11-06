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
; Always offer the builder folder, also on a reinstall (Inno hides it then by default).
DisableDirPage=no
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
Name: "{autoprograms}\Drakan - The Ancients' Gates"; Filename: "{code:GetPlayDir}\drakan.exe"; WorkingDir: "{code:GetPlayDir}"; IconFilename: "{app}\drakan.ico"
Name: "{autodesktop}\Drakan - The Ancients' Gates"; Filename: "{code:GetPlayDir}\drakan.exe"; WorkingDir: "{code:GetPlayDir}"; IconFilename: "{app}\drakan.ico"; Tasks: desktopicon
Name: "{autoprograms}\Rebuild Drakan"; Filename: "{app}\Build-Drakan.cmd"; WorkingDir: "{app}"
Name: "{autoprograms}\Drakan play folder"; Filename: "{code:GetPlayDir}"

[Tasks]
Name: desktopicon; Description: "Create a desktop shortcut"

[Run]
Filename: "{app}\Build-Drakan.cmd"; Parameters: "-IsoPath ""{code:GetIsoPath}""{code:GetBuildOptions}"; WorkingDir: "{app}"; Description: "Build the game now (one to two hours; installs Microsoft's C++ Build Tools if needed)"; Flags: postinstall shellexec nowait skipifsilent

[UninstallDelete]
; Build products; the play folder (saves and settings) is left in place.
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
  PlayPage: TInputDirWizardPage;
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

  PlayPage := CreateInputDirPage(IsoPage.ID,
    'Play folder',
    'Where should the built game go?',
    'The build puts drakan.exe here, and the game keeps its saves and settings here. Rebuilds refresh the game ' +
    'files and keep saves, settings, mods and HD textures. Uninstalling the builder leaves this folder in place.' + #13#10#13#10 +
    'The build itself (about 2 GB of intermediate files) stays in the builder folder chosen before.',
    False, '');
  PlayPage.Add('Play folder:');
  // DrakanPC-Setup.exe /PLAYDIR="D:\Games\DrakanPC" fills it in; otherwise the previous install's choice.
  PlayPage.Values[0] := ExpandConstant('{param:PLAYDIR|}');
  if PlayPage.Values[0] = '' then
    PlayPage.Values[0] := GetPreviousData('PlayFolder', '');

  OptionsPage := CreateInputOptionPage(PlayPage.ID,
    'Optional extras',
    'Choose what else to build.',
    'NVIDIA DLSS works only on NVIDIA RTX graphics cards. Its SDK is downloaded from NVIDIA during the build, under ' +
    'the NVIDIA RTX SDKs License (https://github.com/NVIDIA/DLSS/blob/v310.9.16/LICENSE.txt); ticking the box means you ' +
    'accept that licence.' + #13#10#13#10 +
    'The browser version needs Windows Subsystem for Linux with Ubuntu (wsl --install -d Ubuntu). You play it on ' +
    'the project''s browser player page by choosing the built folder and your .iso; nothing is uploaded.' + #13#10#13#10 +
    'Whole VU1 blocks: the older, slower way to build (up to 17 GB per file); the game runs at the same speed.',
    False, False);
  OptionsPage.Add('NVIDIA DLSS - I accept the NVIDIA RTX SDKs License');
  OptionsPage.Add('Also build the browser version (needs WSL with Ubuntu)');

  CpuPage := CreateInputQueryPage(OptionsPage.ID,
    'Build speed',
    'How much of this PC may the build use?',
    'This PC has ' + GetEnv('NUMBER_OF_PROCESSORS') + ' logical CPU cores. More cores build faster but leave less ' +
    'for other programs; memory still limits how many compilers run at once (each needs up to 2 GB). Leave the ' +
    'field empty to let the builder choose (half the cores, at most 4). These are build settings only: they do not ' +
    'change how the game runs. Affinity applies only to the Windows build; an optional browser build uses its default. "Rebuild Drakan" keeps them.');
  CpuPage.Add('CPU cores to use (empty = automatic):', False);
  // DrakanPC-Setup.exe /CORES=8 fills it in.
  CpuPage.Values[0] := ExpandConstant('{param:CORES|}');
  CpuPage.Add('Windows build affinity hex mask (e.g. 0xFFF; empty = default):', False);
  CpuPage.Values[1] := ExpandConstant('{param:AFFINITY|}');
  OptionsPage.Add('Keep the PC responsive while building (compile at low priority)');
  OptionsPage.Add('Start the game when the build has finished');
  OptionsPage.Add('Compile the VU1 microcode as whole blocks (older method, needs 24 GB or more)');
  // DrakanPC-Setup.exe /VUBLOCKS=whole ticks it; otherwise the last install's choice.
  OptionsPage.Values[4] := (CompareText(ExpandConstant('{param:VUBLOCKS|}'), 'whole') = 0) or
    ((ExpandConstant('{param:VUBLOCKS|}') = '') and (GetPreviousData('VuBlocks', '') = 'Whole'));
end;

function DefaultPlayDir: String;
begin
  Result := AddBackslash(WizardDirValue) + 'dist\DrakanPC';
end;

procedure CurPageChanged(CurPageID: Integer);
begin
  // The builder folder is only known once its page is done.
  if (CurPageID = PlayPage.ID) and (PlayPage.Values[0] = '') then
    PlayPage.Values[0] := DefaultPlayDir;
end;

function GetPlayDir(Param: String): String;
begin
  Result := RemoveBackslashUnlessRoot(Trim(PlayPage.Values[0]));
  if Result = '' then
    Result := DefaultPlayDir;
end;

procedure RegisterPreviousData(PreviousDataKey: Integer);
begin
  SetPreviousData(PreviousDataKey, 'PlayFolder', GetPlayDir(''));
  if OptionsPage.Values[4] then
    SetPreviousData(PreviousDataKey, 'VuBlocks', 'Whole')
  else
    SetPreviousData(PreviousDataKey, 'VuBlocks', 'Chunked');
end;

function NextButtonClick(CurPageID: Integer): Boolean;
var
  Size: Int64;
  Cores, I: Integer;
  Mask: String;
  Nonzero: Boolean;
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
  if CurPageID = PlayPage.ID then begin
    Mask := Lowercase(AddBackslash(GetPlayDir('')));
    if (Length(GetPlayDir('')) <= 3) or (Mask = Lowercase(AddBackslash(WizardDirValue))) or
       (Pos(Lowercase(AddBackslash(WizardDirValue) + 'build\'), Mask) = 1) then begin
      MsgBox('Choose a play folder of its own: not a drive root, the builder folder itself or its build folder.',
        mbError, MB_OK);
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
  if (CurPageID = CpuPage.ID) and Result then begin
    Mask := Lowercase(Trim(CpuPage.Values[1]));
    if Copy(Mask, 1, 2) = '0x' then Delete(Mask, 1, 2);
    if Trim(CpuPage.Values[1]) <> '' then begin
      Nonzero := False;
      if (Length(Mask) < 1) or (Length(Mask) > 16) then Result := False;
      for I := 1 to Length(Mask) do begin
        if Pos(Copy(Mask, I, 1), '0123456789abcdef') = 0 then Result := False;
        if Copy(Mask, I, 1) <> '0' then Nonzero := True;
      end;
      if not Nonzero then Result := False;
      if not Result then
        MsgBox('Affinity must be a nonzero hexadecimal mask of up to 16 digits, such as 0xFFF. The builder checks available processors.', mbError, MB_OK);
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
  if Trim(CpuPage.Values[1]) <> '' then
    Result := Result + ' -CpuAffinity "' + Trim(CpuPage.Values[1]) + '"'
  else
    Result := Result + ' -CpuAffinity default';
  if OptionsPage.Values[2] then
    Result := Result + ' -Priority Low'
  else
    Result := Result + ' -Priority Normal';
  if OptionsPage.Values[3] then
    Result := Result + ' -LaunchWhenDone';
  // Always passed, so a reinstall replaces the choice remembered by an earlier one.
  if OptionsPage.Values[4] then
    Result := Result + ' -VuBlocks Whole'
  else
    Result := Result + ' -VuBlocks Chunked';
  // Always passed, so "Rebuild Drakan" remembers it (build/build-options.json) and the shortcuts match.
  if CompareText(GetPlayDir(''), DefaultPlayDir) = 0 then
    Result := Result + ' -DistDirectory default'
  else
    Result := Result + ' -DistDirectory "' + GetPlayDir('') + '"';
end;
