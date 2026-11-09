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
Filename: "{app}\Build-Drakan.cmd"; Parameters: "-IsoPath ""{code:GetIsoPath}""{code:GetBuildOptions}"; WorkingDir: "{app}"; Description: "Build the game now (one to two hours; installs Microsoft's C++ Build Tools if needed). Near the end the game opens and closes by itself several times to take pictures; leave it alone"; Flags: postinstall shellexec nowait skipifsilent

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
  OotfPage: TWizardPage;
  OotfEdit: TNewEdit;
  DlssPage: TInputOptionWizardPage;
  HdPage: TInputOptionWizardPage;
  DefaultsPage: TInputOptionWizardPage;
  OptionsPage: TInputOptionWizardPage;
  OotfRejected: Boolean;
  CpuPage: TInputQueryWizardPage;

procedure OotfBrowseFolder(Sender: TObject);
var
  Dir: String;
begin
  Dir := OotfEdit.Text;
  if BrowseForFolder('Choose the folder Drakan: Order of the Flame is installed in (or a copy of its disc)', Dir, False) then
    OotfEdit.Text := Dir;
end;

procedure OotfBrowseFile(Sender: TObject);
var
  Name: String;
begin
  Name := OotfEdit.Text;
  if GetOpenFileName('Choose the Drakan: Order of the Flame disc image', Name, '',
    'Disc images and zips (*.mdx;*.iso;*.zip)|*.mdx;*.iso;*.zip|All files (*.*)|*.*', 'mdx') then
    OotfEdit.Text := Name;
end;

procedure CreateOotfPage;
var
  Info: TNewStaticText;
  FolderButton, FileButton: TNewButton;
begin
  OotfPage := CreateCustomPage(PlayPage.ID,
    'Drakan: Order of the Flame (optional)',
    'Add the first Drakan as a second campaign.');
  Info := TNewStaticText.Create(OotfPage);
  Info.Parent := OotfPage.Surface;
  Info.WordWrap := True;
  Info.Width := OotfPage.SurfaceWidth;
  Info.Caption :=
    'If you own the PC game Drakan: Order of the Flame, the builder can convert its 13 campaign levels and 10 ' +
    'multiplayer arenas, with their cut scenes and music, into a mod pack for this game. The title menu then offers ' +
    'both games. Choose its install folder (the one holding Dragon.rrc), a copy of the disc, its disc image ' +
    '(.mdx or .iso) or the Drakan.zip that holds the image. Leave the field empty to skip it.' + #13#10#13#10 +
    'The conversion runs on this PC from your own files, adds about 3 to 8 min to the build and needs about 1 GB ' +
    'of disk space. Nothing is uploaded and no game data is included in this setup.';
  OotfEdit := TNewEdit.Create(OotfPage);
  OotfEdit.Parent := OotfPage.Surface;
  OotfEdit.Top := Info.Top + Info.Height + ScaleY(16);
  OotfEdit.Left := 0;
  OotfEdit.Width := OotfPage.SurfaceWidth;
  // DrakanPC-Setup.exe /OOTF="D:\Games\Drakan" fills it in.
  OotfEdit.Text := ExpandConstant('{param:OOTF|}');
  FolderButton := TNewButton.Create(OotfPage);
  FolderButton.Parent := OotfPage.Surface;
  FolderButton.Caption := 'Choose install folder...';
  FolderButton.Top := OotfEdit.Top + OotfEdit.Height + ScaleY(8);
  FolderButton.Left := 0;
  FolderButton.Width := ScaleX(150);
  FolderButton.Height := ScaleY(23);
  FolderButton.OnClick := @OotfBrowseFolder;
  FileButton := TNewButton.Create(OotfPage);
  FileButton.Parent := OotfPage.Surface;
  FileButton.Caption := 'Choose disc image or zip...';
  FileButton.Top := FolderButton.Top;
  FileButton.Left := FolderButton.Left + FolderButton.Width + ScaleX(8);
  FileButton.Width := ScaleX(170);
  FileButton.Height := ScaleY(23);
  FileButton.OnClick := @OotfBrowseFile;
end;

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

  CreateOotfPage;

  DlssPage := CreateInputOptionPage(OotfPage.ID,
    'NVIDIA DLSS (optional)',
    'AI upscaling and frame generation on NVIDIA RTX graphics cards.',
    'DLSS works only on NVIDIA RTX graphics cards; on any other card leave it off. Its SDK is downloaded from ' +
    'NVIDIA during the build, under the NVIDIA RTX SDKs License ' +
    '(https://github.com/NVIDIA/DLSS/blob/v310.9.16/LICENSE.txt); ticking the box means you accept that licence. ' +
    'Without it the game still runs on every graphics card.',
    False, False);
  DlssPage.Add('NVIDIA DLSS - I accept the NVIDIA RTX SDKs License');

  HdPage := CreateInputOptionPage(DlssPage.ID,
    'HD textures (optional)',
    'Upscale every texture with an AI upscaler on your graphics card.',
    'After the build every texture of the game (and of Order of the Flame, when you chose it) is upscaled 4x with the ' +
    'free Real-ESRGAN AI upscaler. It runs on any graphics card with Vulkan drivers and downloads about 75 MB of tools ' +
    'once, checked against pinned hashes. About 11 to 20 min on a fast graphics card (about 2 GB), ' +
    '17 to 31 min together with Order of the Flame (about 4 GB); several times longer on an older card and ' +
    'hours on integrated graphics. The game plays without them, the step can be stopped and resumed, and the build can ' +
    'be run again to make them later. The build window shows the estimate for your own PC before it starts.',
    False, False);
  HdPage.Add('Make HD textures after the build (needs a Vulkan graphics card)');
  // DrakanPC-Setup.exe /HD=1 ticks it.
  HdPage.Values[0] := ExpandConstant('{param:HD|}') = '1';

  DefaultsPage := CreateInputOptionPage(HdPage.ID,
    'Game options (optional)',
    'Choose how the game starts. You can change all of these in the game later.',
    'These are only starting values. Press F1 in the game for the PC settings, ' +
    'or edit drakan.ini and drakan_input.ini in the play folder, to change any of them at any time. Leave a box empty to ' +
    'keep the original behaviour.',
    False, False);
  DefaultsPage.Add('Modernise the menus (the PC main menu over the title art). Leave empty for the original PS2 menus.');
  DefaultsPage.Add('Use the Order of the Flame bow combat: left click fires with auto-aim, right click toggles the sight.');
  DefaultsPage.Add('Enable dual wielding (experimental): a second one-handed weapon goes in the left hand.');
  // DrakanPC-Setup.exe /MENUS=modern, /BOW=ootf and /DUAL=1 tick them; the menus default to modern.
  DefaultsPage.Values[0] := CompareText(ExpandConstant('{param:MENUS|modern}'), 'original') <> 0;
  DefaultsPage.Values[1] := CompareText(ExpandConstant('{param:BOW|}'), 'ootf') = 0;
  DefaultsPage.Values[2] := ExpandConstant('{param:DUAL|}') = '1';

  OptionsPage := CreateInputOptionPage(DefaultsPage.ID,
    'Other options',
    'Browser version, build priority and the VU1 build method.',
    'The browser version needs Windows Subsystem for Linux with Ubuntu (wsl --install -d Ubuntu). You play it on ' +
    'the project''s browser player page by choosing the built folder and your .iso; nothing is uploaded.' + #13#10#13#10 +
    'VU1 microcode: the PlayStation 2 has a second vector processor (VU1) that runs much of the game''s 3D geometry ' +
    'code. The builder translates that microcode into C++ and compiles it. By default this is done in small chunks that ' +
    'need about 2 GB of memory per compiler, which suits any PC. "Whole blocks" is the older method: four very large ' +
    'files, each needing up to 17 GB of memory and 10 to 20 minutes, compiled one after another. The game runs at ' +
    'the same speed either way, so tick it only if you have 24 GB or more of memory and a reason to.',
    False, False);
  OptionsPage.Add('Also build the browser version (needs WSL with Ubuntu)');
  OptionsPage.Add('Keep the PC responsive while building (compile at low priority)');
  OptionsPage.Add('Start the game when the build has finished');
  OptionsPage.Add('Compile the VU1 microcode as whole blocks (older method, needs 24 GB or more)');
  // DrakanPC-Setup.exe /VUBLOCKS=whole ticks it; otherwise the last install's choice.
  OptionsPage.Values[3] := (CompareText(ExpandConstant('{param:VUBLOCKS|}'), 'whole') = 0) or
    ((ExpandConstant('{param:VUBLOCKS|}') = '') and (GetPreviousData('VuBlocks', '') = 'Whole'));

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
  if OptionsPage.Values[3] then
    SetPreviousData(PreviousDataKey, 'VuBlocks', 'Whole')
  else
    SetPreviousData(PreviousDataKey, 'VuBlocks', 'Chunked');
end;

// What is wrong with a Drakan: Order of the Flame choice, or '' when it looks usable. The builder checks the contents
// again (scripts/buildtool/extras.py check-source --deep) once the files are installed, before the build starts.
function OotfProblem(Path: String): String;
var
  Found: TFindRec;
  Ext: String;
  FileBytes: Int64;
begin
  Result := '';
  Path := RemoveBackslashUnlessRoot(Path);
  if DirExists(Path) then begin
    if FileExists(Path + '\Dragon.rrc') or FileExists(Path + '\SETUP\DATA1.CAB') or FileExists(Path + '\DATA1.CAB') then
      Exit;
    if FindFirst(Path + '\*', Found) then begin
      try
        repeat
          if ((Found.Attributes and FILE_ATTRIBUTE_DIRECTORY) <> 0) and (Found.Name <> '.') and (Found.Name <> '..') then
            if FileExists(Path + '\' + Found.Name + '\Dragon.rrc') then
              Exit;
        until not FindNext(Found);
      finally
        FindClose(Found);
      end;
    end;
    Result := 'That folder holds neither the installed game (Dragon.rrc) nor a copy of its disc (SETUP\DATA1.CAB).';
  end else if FileExists(Path) then begin
    Ext := Lowercase(ExtractFileExt(Path));
    if (Ext <> '.zip') and (Ext <> '.mdx') and (Ext <> '.iso') and (Ext <> '.cue') and (Ext <> '.bin') then
      Result := 'Choose the installed game folder, a disc image (.mdx, .iso, or .cue with its .bin tracks) or a .zip holding one.'
    else if (Ext <> '.cue') and FileSize64(Path, FileBytes) and (FileBytes < 50 * 1024 * 1024) then
      Result := 'That file is too small to be the game disc. For a .cue / .bin rip choose the .cue sheet or Track 1.';
  end else
    Result := 'That file or folder does not exist.';
end;

function NextButtonClick(CurPageID: Integer): Boolean;
var
  Problem: String;
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
  if (CurPageID = OotfPage.ID) and (Trim(OotfEdit.Text) <> '') then begin
    Problem := OotfProblem(Trim(OotfEdit.Text));
    if Problem <> '' then begin
      MsgBox(Problem + #13#10#13#10 + 'Choose your Drakan: Order of the Flame folder, disc image or zip, ' +
        'or leave the field empty to skip it.', mbError, MB_OK);
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

procedure CurStepChanged(CurStep: TSetupStep);
var
  Log, Text: String;
  Output: AnsiString;
  Code: Integer;
begin
  // The files are installed, so the builder's own checker can read the choice (a zip's contents, a disc's cabinet).
  // A choice that does not work is left out of the build instead of stopping it. Silent installs build nothing.
  if (CurStep = ssPostInstall) and (Trim(OotfEdit.Text) <> '') and not WizardSilent then begin
    WizardForm.StatusLabel.Caption := 'Checking your Drakan: Order of the Flame files...';
    Log := ExpandConstant('{tmp}\ootf-check.txt');
    DeleteFile(Log);
    OotfRejected := True;
    if Exec(ExpandConstant('{cmd}'),
      '/C ""' + ExpandConstant('{app}\tools\python\python.exe') + '" -I "' +
      ExpandConstant('{app}\scripts\buildtool\extras.py') + '" check-source --deep "' + Trim(OotfEdit.Text) +
      '" > "' + Log + '" 2>&1"', '', SW_HIDE, ewWaitUntilTerminated, Code) then
      OotfRejected := Code <> 0;
    if OotfRejected then begin
      Text := '';
      if LoadStringFromFile(Log, Output) then
        Text := Trim(String(Output));
      MsgBox('Your Drakan: Order of the Flame choice cannot be used:' + #13#10#13#10 + Text + #13#10#13#10 +
        'The game itself is still built, without Order of the Flame. To add it later, run "Rebuild Drakan" from a ' +
        'command prompt with -OotfSource <path>, or run this setup again with a working copy.', mbError, MB_OK);
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
  if DlssPage.Values[0] then
    Result := Result + ' -EnableDlss';
  if OptionsPage.Values[0] then
    Result := Result + ' -Web';
  // Always passed, so a reinstall replaces the choice remembered by an earlier one.
  Result := Result + ' -Cores ' + IntToStr(StrToIntDef(Trim(CpuPage.Values[0]), 0));
  if Trim(CpuPage.Values[1]) <> '' then
    Result := Result + ' -CpuAffinity "' + Trim(CpuPage.Values[1]) + '"'
  else
    Result := Result + ' -CpuAffinity default';
  if OptionsPage.Values[1] then
    Result := Result + ' -Priority Low'
  else
    Result := Result + ' -Priority Normal';
  if OptionsPage.Values[2] then
    Result := Result + ' -LaunchWhenDone';
  // Always passed, so a reinstall replaces the choice remembered by an earlier one.
  if OptionsPage.Values[3] then
    Result := Result + ' -VuBlocks Whole'
  else
    Result := Result + ' -VuBlocks Chunked';
  // Always passed, so a reinstall replaces what an earlier one remembered. The builder checks the source and says so.
  if (Trim(OotfEdit.Text) <> '') and not OotfRejected then
    Result := Result + ' -OotfSource "' + Trim(OotfEdit.Text) + '"'
  else
    Result := Result + ' -OotfSource none';
  // Always passed, so a reinstall applies what this page says; a plain rebuild leaves the settings alone.
  if DefaultsPage.Values[0] then
    Result := Result + ' -MenuStyle modern'
  else
    Result := Result + ' -MenuStyle original';
  if DefaultsPage.Values[1] then
    Result := Result + ' -BowCombat ootf'
  else
    Result := Result + ' -BowCombat native';
  if DefaultsPage.Values[2] then
    Result := Result + ' -DualWield on'
  else
    Result := Result + ' -DualWield off';
  if HdPage.Values[0] then
    Result := Result + ' -HdTextures on'
  else
    Result := Result + ' -HdTextures off';
  // Always passed, so "Rebuild Drakan" remembers it (build/build-options.json) and the shortcuts match.
  if CompareText(GetPlayDir(''), DefaultPlayDir) = 0 then
    Result := Result + ' -DistDirectory default'
  else
    Result := Result + ' -DistDirectory "' + GetPlayDir('') + '"';
end;
