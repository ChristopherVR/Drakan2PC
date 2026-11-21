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
#ifndef ReleaseTag
  #define ReleaseTag ""
#endif
#define SetupRegistration "Software\DrakanPC\Setup"
#define AppIcon "ps2recomp\ps2xRuntime\src\res\drakan.ico"

[Setup]
AppId={{69932327-BBAB-42A2-AA60-D15FFF9D83E6}
AppName=Drakan: The Ancients' Gates (PC builder)
AppVersion={#AppVersion}
AppVerName=Drakan: The Ancients' Gates PC builder {#AppVersion}
#if ReleaseTag != ""
VersionInfoProductName=Drakan PC Builder
VersionInfoProductTextVersion={#ReleaseTag}
VersionInfoProductVersion={#ReleaseProductVersion}
#endif
DefaultDirName={localappdata}\DrakanPC
; Always offer the builder folder, also on a reinstall (Inno hides it then by default).
DisableDirPage=no
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
SetupMutex=DrakanPC-Setup
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
OutputDir={#OutputDir}
OutputBaseFilename={#OutputName}
SetupIconFile={#StageDir}\{#AppIcon}
UninstallDisplayIcon={app}\{#AppIcon}
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
; The build folder holds about 2 GB once the game is built.
ExtraDiskSpaceRequired=2147483648

[Languages]
Name: "en"; MessagesFile: "compiler:Default.isl,locales\en\setup.isl"; InfoBeforeFile: "locales\en\SETUP-INFO.txt"
Name: "de"; MessagesFile: "compiler:Languages\German.isl,locales\de\setup.isl"; InfoBeforeFile: "locales\de\SETUP-INFO.txt"
Name: "fr"; MessagesFile: "compiler:Languages\French.isl,locales\fr\setup.isl"; InfoBeforeFile: "locales\fr\SETUP-INFO.txt"
Name: "it"; MessagesFile: "compiler:Languages\Italian.isl,locales\it\setup.isl"; InfoBeforeFile: "locales\it\SETUP-INFO.txt"
Name: "es"; MessagesFile: "compiler:Languages\Spanish.isl,locales\es\setup.isl"; InfoBeforeFile: "locales\es\SETUP-INFO.txt"

[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Excludes: "\README.txt"; Flags: recursesubdirs ignoreversion
Source: "locales\en\README.txt"; DestDir: "{app}"; DestName: "README.txt"; Languages: en; Flags: ignoreversion
Source: "locales\de\README.txt"; DestDir: "{app}"; DestName: "README.txt"; Languages: de; Flags: ignoreversion
Source: "locales\fr\README.txt"; DestDir: "{app}"; DestName: "README.txt"; Languages: fr; Flags: ignoreversion
Source: "locales\it\README.txt"; DestDir: "{app}"; DestName: "README.txt"; Languages: it; Flags: ignoreversion
Source: "locales\es\README.txt"; DestDir: "{app}"; DestName: "README.txt"; Languages: es; Flags: ignoreversion

[Registry]
#if ReleaseTag != ""
Root: HKCU64; Subkey: "{#SetupRegistration}"; ValueType: string; ValueName: "BuilderDir"; ValueData: "{app}"; Flags: uninsdeletevalue uninsdeletekeyifempty
Root: HKCU64; Subkey: "{#SetupRegistration}"; ValueType: string; ValueName: "PlayDir"; ValueData: "{code:GetPlayDir}"; Flags: uninsdeletevalue uninsdeletekeyifempty
#else
Root: HKCU64; Subkey: "{#SetupRegistration}"; ValueType: none; Flags: deletekey
#endif

[Icons]
Name: "{autoprograms}\Drakan - The Ancients' Gates"; Filename: "{code:GetPlayDir}\drakan.exe"; WorkingDir: "{code:GetPlayDir}"; IconFilename: "{app}\{#AppIcon}"
Name: "{autodesktop}\Drakan - The Ancients' Gates"; Filename: "{code:GetPlayDir}\drakan.exe"; WorkingDir: "{code:GetPlayDir}"; IconFilename: "{app}\{#AppIcon}"; Tasks: desktopicon
Name: "{autoprograms}\{cm:RebuildShortcut}"; Filename: "{app}\Build-Drakan.cmd"; WorkingDir: "{app}"
Name: "{autoprograms}\{cm:PlayShortcut}"; Filename: "{code:GetPlayDir}"

[Tasks]
Name: desktopicon; Description: "{cm:CreateDesktopIcon}"

[Run]
Filename: "{app}\Build-Drakan.cmd"; Parameters: "-Language {language} -IsoPath ""{code:GetIsoPath}""{code:GetBuildOptions}"; WorkingDir: "{app}"; Description: "{cm:BuildNow}"; Flags: postinstall shellexec nowait skipifsilent

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
const
  SetupRegistration = '{#SetupRegistration}';
  SetupRelease = '{#ReleaseTag}';
  Synchronize = $00100000;
  InvalidProcessId = 87;
  WaitSignaled = 0;
  GameExitTimeoutMs = 60000;
  GenericReadWrite = $C0000000;
  OpenAlways = 4;
  NormalFile = $80;

var
  UpdateMode: Boolean;
  UpdateBuilderDir: String;
  UpdatePlayDir: String;
  BuildLockHandle: THandle;
  UpdateExitCode: Integer;
  IsoPage: TInputFileWizardPage;
  PlayPage: TInputDirWizardPage;
  OotfPage: TWizardPage;
  SubtitlePage: TInputOptionWizardPage;
  OotfEdit: TNewEdit;
  DlssPage: TInputOptionWizardPage;
  HdPage: TInputOptionWizardPage;
  DefaultsPage: TInputOptionWizardPage;
  OptionsPage: TInputOptionWizardPage;
  OotfRejected: Boolean;
  CpuPage: TInputQueryWizardPage;

function OpenGameProcess(Access, InheritHandle, ProcessId: Cardinal): THandle;
  external 'OpenProcess@kernel32.dll stdcall';
function WaitForGameProcess(Handle: THandle; Milliseconds: Cardinal): Cardinal;
  external 'WaitForSingleObject@kernel32.dll stdcall';
function CloseKernelHandle(Handle: THandle): Cardinal;
  external 'CloseHandle@kernel32.dll stdcall';
function OpenBuildLock(const Name: String; Access, Share: Cardinal; Security: THandle;
  Creation, Flags: Cardinal; Template: THandle): THandle;
  external 'CreateFileW@kernel32.dll stdcall';

function SameDirectory(Left, Right: String): Boolean;
begin
  Result := (Left <> '') and (Right <> '') and
    SameText(RemoveBackslashUnlessRoot(ExpandFileName(Left)), RemoveBackslashUnlessRoot(ExpandFileName(Right)));
end;

function WaitForGameExit: Boolean;
var
  Pid: Int64;
  Process: THandle;
begin
  Result := False;
  Pid := StrToInt64Def(ExpandConstant('{param:DRAKANWAITPID|}'), 0);
  if (Pid <= 0) or (Pid > $FFFFFFFF) then
    Exit;
  Process := OpenGameProcess(Synchronize, 0, Cardinal(Pid));
  if Process = 0 then begin
    Result := DLLGetLastError = InvalidProcessId;
    Exit;
  end;
  try
    Result := WaitForGameProcess(Process, GameExitTimeoutMs) = WaitSignaled;
  finally
    CloseKernelHandle(Process);
  end;
end;

procedure ReleaseBuildLock;
begin
  if BuildLockHandle <> 0 then begin
    CloseKernelHandle(BuildLockHandle);
    BuildLockHandle := 0;
  end;
end;

function InitializeSetup: Boolean;
var
  BuildDir: String;
begin
  UpdateMode := ExpandConstant('{param:DRAKANUPDATE|}') = '1';
  Result := True;
  if not UpdateMode then
    Exit;
  Result := False;
  if (SetupRelease = '') or
     not RegQueryStringValue(HKCU64, SetupRegistration, 'BuilderDir', UpdateBuilderDir) or
     not RegQueryStringValue(HKCU64, SetupRegistration, 'PlayDir', UpdatePlayDir) or
     not SameDirectory(UpdateBuilderDir, ExpandConstant('{param:DIR|}')) or
     not SameDirectory(UpdatePlayDir, ExpandConstant('{param:PLAYDIR|}')) or
     not FileExists(AddBackslash(UpdatePlayDir) + 'drakan.exe') then begin
    SuppressibleMsgBox(CustomMessage('UpdateMismatch'), mbError, MB_OK, IDOK);
    Exit;
  end;
  if not WaitForGameExit then begin
    SuppressibleMsgBox(CustomMessage('UpdateGameOpen'), mbError, MB_OK, IDOK);
    Exit;
  end;

  BuildDir := AddBackslash(UpdateBuilderDir) + 'build\';
  if not FileExists(BuildDir + 'iso-path.txt') then begin
    SuppressibleMsgBox(CustomMessage('UpdateMissingDisc'),
      mbError, MB_OK, IDOK);
    Exit;
  end;
  // Build-LocalGame.ps1 uses the same file while compiling or writing this installation's play folder.
  BuildLockHandle := OpenBuildLock(BuildDir + 'game.build.lock', GenericReadWrite, 0, 0, OpenAlways, NormalFile, 0);
  if BuildLockHandle = THandle(-1) then begin
    BuildLockHandle := 0;
    SuppressibleMsgBox(CustomMessage('UpdateBuildBusy'),
      mbError, MB_OK, IDOK);
    Exit;
  end;
  Result := True;
end;

procedure DeinitializeSetup;
begin
  ReleaseBuildLock;
end;

function GetCustomSetupExitCode: Integer;
begin
  Result := UpdateExitCode;
end;

procedure OotfBrowseFolder(Sender: TObject);
var
  Dir: String;
begin
  Dir := OotfEdit.Text;
  if BrowseForFolder(CustomMessage('OotfBrowseFolder'), Dir, False) then
    OotfEdit.Text := Dir;
end;

procedure OotfBrowseFile(Sender: TObject);
var
  Name: String;
begin
  Name := OotfEdit.Text;
  if GetOpenFileName(CustomMessage('OotfBrowseImage'), Name, '',
    CustomMessage('OotfFilter'), 'mdx') then
    OotfEdit.Text := Name;
end;

procedure CreateOotfPage;
var
  Info: TNewStaticText;
  FolderButton, FileButton: TNewButton;
begin
  OotfPage := CreateCustomPage(PlayPage.ID,
    CustomMessage('OotfTitle'),
    CustomMessage('OotfSubtitle'));
  Info := TNewStaticText.Create(OotfPage);
  Info.Parent := OotfPage.Surface;
  Info.WordWrap := True;
  Info.Width := OotfPage.SurfaceWidth;
  Info.Caption := CustomMessage('OotfDescription');
  WizardForm.AdjustLabelHeight(Info);
  OotfEdit := TNewEdit.Create(OotfPage);
  OotfEdit.Parent := OotfPage.Surface;
  OotfEdit.Top := Info.Top + Info.Height + ScaleY(16);
  OotfEdit.Left := 0;
  OotfEdit.Width := OotfPage.SurfaceWidth;
  // DrakanPC-Setup.exe /OOTF="D:\Games\Drakan" fills it in.
  OotfEdit.Text := ExpandConstant('{param:OOTF|}');
  FolderButton := TNewButton.Create(OotfPage);
  FolderButton.Parent := OotfPage.Surface;
  FolderButton.Caption := CustomMessage('OotfFolderButton');
  FolderButton.Top := OotfEdit.Top + OotfEdit.Height + ScaleY(8);
  FolderButton.Left := 0;
  FolderButton.Width := (OotfPage.SurfaceWidth - ScaleX(8)) div 2;
  FolderButton.Height := ScaleY(23);
  FolderButton.OnClick := @OotfBrowseFolder;
  FileButton := TNewButton.Create(OotfPage);
  FileButton.Parent := OotfPage.Surface;
  FileButton.Caption := CustomMessage('OotfImageButton');
  FileButton.Top := FolderButton.Top;
  FileButton.Left := FolderButton.Left + FolderButton.Width + ScaleX(8);
  FileButton.Width := OotfPage.SurfaceWidth - FileButton.Left;
  FileButton.Height := ScaleY(23);
  FileButton.OnClick := @OotfBrowseFile;
end;

procedure InitializeWizard;
begin
  IsoPage := CreateInputFilePage(wpSelectDir,
    CustomMessage('DiscTitle'), CustomMessage('DiscSubtitle'),
    CustomMessage('DiscDescription'));
  IsoPage.Add(CustomMessage('DiscField'), CustomMessage('DiscFilter'), '.iso');
  // DrakanPC-Setup.exe /ISO="D:\Games\Drakan.iso" fills it in.
  IsoPage.Values[0] := ExpandConstant('{param:ISO|}');

  PlayPage := CreateInputDirPage(IsoPage.ID,
    CustomMessage('PlayTitle'), CustomMessage('PlaySubtitle'),
    CustomMessage('PlayDescription'), False, '');
  PlayPage.Add(CustomMessage('PlayField'));
  // DrakanPC-Setup.exe /PLAYDIR="D:\Games\DrakanPC" fills it in; otherwise the previous install's choice.
  PlayPage.Values[0] := ExpandConstant('{param:PLAYDIR|}');
  if PlayPage.Values[0] = '' then
    PlayPage.Values[0] := GetPreviousData('PlayFolder', '');

  CreateOotfPage;

  SubtitlePage := CreateInputOptionPage(OotfPage.ID,
    CustomMessage('SubtitlesTitle'), CustomMessage('SubtitlesSubtitle'),
    CustomMessage('SubtitlesDescription'), True, False);
  SubtitlePage.Add(CustomMessage('SubtitlesBundled'));
  SubtitlePage.Add(CustomMessage('SubtitlesGenerate'));
  SubtitlePage.Values[1] := GetPreviousData('OotfSubtitles', 'bundled') = 'generate';
  SubtitlePage.Values[0] := not SubtitlePage.Values[1];

  DlssPage := CreateInputOptionPage(SubtitlePage.ID,
    CustomMessage('DlssTitle'), CustomMessage('DlssSubtitle'),
    CustomMessage('DlssDescription'), False, False);
  DlssPage.Add(CustomMessage('DlssOption'));

  HdPage := CreateInputOptionPage(DlssPage.ID,
    CustomMessage('HdTitle'), CustomMessage('HdSubtitle'),
    CustomMessage('HdDescription'), False, False);
  HdPage.Add(CustomMessage('HdOption'));
  // DrakanPC-Setup.exe /HD=1 ticks it.
  HdPage.Values[0] := ExpandConstant('{param:HD|}') = '1';

  DefaultsPage := CreateInputOptionPage(HdPage.ID,
    CustomMessage('DefaultsTitle'), CustomMessage('DefaultsSubtitle'),
    CustomMessage('DefaultsDescription'), False, False);
  DefaultsPage.Add(CustomMessage('DefaultsMenus'));
  DefaultsPage.Add(CustomMessage('DefaultsBow'));
  DefaultsPage.Add(CustomMessage('DefaultsDual'));
  DefaultsPage.Add(CustomMessage('DefaultsIssue'));
  // DrakanPC-Setup.exe /MENUS=modern, /BOW=ootf and /DUAL=1 tick them; the menus default to modern.
  DefaultsPage.Values[0] := CompareText(ExpandConstant('{param:MENUS|modern}'), 'original') <> 0;
  DefaultsPage.Values[1] := CompareText(ExpandConstant('{param:BOW|}'), 'ootf') = 0;
  DefaultsPage.Values[2] := ExpandConstant('{param:DUAL|}') = '1';
  DefaultsPage.Values[3] := ExpandConstant('{param:ISSUEREPORTING|}') <> '0';
  if ExpandConstant('{param:ISSUEREPORTING|}') = '' then
    DefaultsPage.Values[3] := GetPreviousData('IssueReporting', '1') <> '0';

  OptionsPage := CreateInputOptionPage(DefaultsPage.ID,
    CustomMessage('OptionsTitle'), CustomMessage('OptionsSubtitle'),
    CustomMessage('OptionsDescription'), False, False);
  OptionsPage.Add(CustomMessage('OptionsWeb'));
  OptionsPage.Add(CustomMessage('OptionsPriority'));
  OptionsPage.Add(CustomMessage('OptionsLaunch'));
  OptionsPage.Add(CustomMessage('OptionsVuWhole'));
  // DrakanPC-Setup.exe /VUBLOCKS=whole ticks it; otherwise the last install's choice.
  OptionsPage.Values[3] := (CompareText(ExpandConstant('{param:VUBLOCKS|}'), 'whole') = 0) or
    ((ExpandConstant('{param:VUBLOCKS|}') = '') and (GetPreviousData('VuBlocks', '') = 'Whole'));

  CpuPage := CreateInputQueryPage(OptionsPage.ID,
    CustomMessage('CpuTitle'), CustomMessage('CpuSubtitle'),
    FmtMessage(CustomMessage('CpuDescription'), [GetEnv('NUMBER_OF_PROCESSORS')]));
  CpuPage.Add(CustomMessage('CpuCores'), False);
  // DrakanPC-Setup.exe /CORES=8 fills it in.
  CpuPage.Values[0] := ExpandConstant('{param:CORES|}');
  CpuPage.Add(CustomMessage('CpuAffinity'), False);
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
  if UpdateMode then
    Exit;
  if SubtitlePage.Values[1] then
    SetPreviousData(PreviousDataKey, 'OotfSubtitles', 'generate')
  else
    SetPreviousData(PreviousDataKey, 'OotfSubtitles', 'bundled');
  SetPreviousData(PreviousDataKey, 'PlayFolder', GetPlayDir(''));
  if DefaultsPage.Values[3] then
    SetPreviousData(PreviousDataKey, 'IssueReporting', '1')
  else
    SetPreviousData(PreviousDataKey, 'IssueReporting', '0');
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
    Result := CustomMessage('OotfBadFolder');
  end else if FileExists(Path) then begin
    Ext := Lowercase(ExtractFileExt(Path));
    if (Ext <> '.zip') and (Ext <> '.mdx') and (Ext <> '.iso') and (Ext <> '.cue') and (Ext <> '.bin') then
      Result := CustomMessage('OotfBadFormat')
    else if (Ext <> '.cue') and FileSize64(Path, FileBytes) and (FileBytes < 50 * 1024 * 1024) then
      Result := CustomMessage('OotfSmallFile');
  end else
    Result := CustomMessage('OotfMissing');
end;

function ShouldSkipPage(PageID: Integer): Boolean;
begin
  Result := UpdateMode or ((PageID = SubtitlePage.ID) and (Trim(OotfEdit.Text) = ''));
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  Result := '';
  if UpdateMode and
    (not SameDirectory(UpdateBuilderDir, WizardDirValue) or not SameDirectory(UpdatePlayDir, GetPlayDir(''))) then
    Result := CustomMessage('UpdatePaths');
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
  if UpdateMode then
    Exit;
  // A regular silent install never starts the build (skipifsilent), so it does not need an ISO.
  if (CurPageID = IsoPage.ID) and not (WizardSilent and (IsoPage.Values[0] = '')) then begin
    if not FileExists(IsoPage.Values[0]) then begin
      MsgBox(CustomMessage('DiscMissing'), mbError, MB_OK);
      Result := False;
    end else if FileSize64(IsoPage.Values[0], Size) and (Size < 100 * 1024 * 1024) then begin
      MsgBox(CustomMessage('DiscSmall'), mbError, MB_OK);
      Result := False;
    end;
  end;
  if (CurPageID = OotfPage.ID) and (Trim(OotfEdit.Text) <> '') then begin
    Problem := OotfProblem(Trim(OotfEdit.Text));
    if Problem <> '' then begin
      MsgBox(FmtMessage(CustomMessage('OotfInvalid'), [Problem]), mbError, MB_OK);
      Result := False;
    end;
  end;
  if CurPageID = PlayPage.ID then begin
    Mask := Lowercase(AddBackslash(GetPlayDir('')));
    if (Length(GetPlayDir('')) <= 3) or (Mask = Lowercase(AddBackslash(WizardDirValue))) or
       (Pos(Lowercase(AddBackslash(WizardDirValue) + 'build\'), Mask) = 1) then begin
      MsgBox(CustomMessage('PlayBadPath'),
        mbError, MB_OK);
      Result := False;
    end;
  end;
  if (CurPageID = CpuPage.ID) and (Trim(CpuPage.Values[0]) <> '') then begin
    Cores := StrToIntDef(Trim(CpuPage.Values[0]), 0);
    if (Cores < 1) or (Cores > StrToIntDef(GetEnv('NUMBER_OF_PROCESSORS'), 256)) then begin
      MsgBox(FmtMessage(CustomMessage('CpuInvalidCores'), [GetEnv('NUMBER_OF_PROCESSORS')]), mbError, MB_OK);
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
        MsgBox(CustomMessage('CpuInvalidAffinity'), mbError, MB_OK);
    end;
  end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  Log, Text: String;
  Output: AnsiString;
  Code: Integer;
begin
  if (CurStep = ssPostInstall) and UpdateMode then begin
    WizardForm.StatusLabel.Caption := CustomMessage('UpdateBuilding');
    ReleaseBuildLock;
    if not Exec(
      ExpandConstant('{app}\tools\pwsh\pwsh.exe'),
      '-NoProfile -ExecutionPolicy Bypass -File "' +
        ExpandConstant('{app}\scripts\buildtool\Build-Drakan.ps1') +
        '" -TransactionalUpdate -DistDirectory "' + UpdatePlayDir + '"',
      UpdateBuilderDir, SW_SHOWNORMAL, ewWaitUntilTerminated, Code) then
      Code := 1;
    if Code <> 0 then begin
      UpdateExitCode := 20;
      SuppressibleMsgBox(FmtMessage(CustomMessage('UpdateFailed'), [AddBackslash(UpdateBuilderDir) + 'build-log.txt']),
        mbError, MB_OK, IDOK);
    end;
    Exit;
  end;

  // The files are installed, so the builder's own checker can read the choice (a zip's contents, a disc's cabinet).
  // A choice that does not work is left out of the build instead of stopping it. Regular silent installs build nothing.
  if (CurStep = ssPostInstall) and (Trim(OotfEdit.Text) <> '') and not WizardSilent then begin
    WizardForm.StatusLabel.Caption := CustomMessage('OotfChecking');
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
      MsgBox(FmtMessage(CustomMessage('OotfRejected'), [Text]), mbError, MB_OK);
    end;
  end;
end;

// Builds up to v0.1.11 saved into build\game (next to the game's ELF) instead of the play folder, and the uninstaller
// deletes the build folder. Before it does, the builder's own script copies those saves into the play folder; it
// never overwrites or deletes a save (scripts/buildtool/migrate_saves.py). When that fails the player decides.
// Any card folder (mc0, mc1, the campaign cards mc0_<campaign>) directly in Folder.
function HasCardFolder(Folder: String): Boolean;
var
  Found: TFindRec;
begin
  Result := False;
  if FindFirst(Folder + '\mc*', Found) then
    try
      repeat
        if ((Found.Attributes and FILE_ATTRIBUTE_DIRECTORY) <> 0) and
          ((CompareText(Found.Name, 'mc0') = 0) or (CompareText(Found.Name, 'mc1') = 0) or
           (CompareText(Copy(Found.Name, 1, 4), 'mc0_') = 0) or (CompareText(Copy(Found.Name, 1, 4), 'mc1_') = 0)) then
          Result := True;
      until Result or not FindNext(Found);
    finally
      FindClose(Found);
    end;
end;

function InitializeUninstall(): Boolean;
var
  Python, Script, Work, Play: String;
  Code: Integer;
begin
  Result := True;
  Python := ExpandConstant('{app}\tools\python\python.exe');
  Script := ExpandConstant('{app}\scripts\buildtool\migrate_saves.py');
  Work := ExpandConstant('{app}\build\game');
  Play := GetPreviousData('PlayFolder', ExpandConstant('{app}\dist\DrakanPC'));
  if not DirExists(Work) then
    Exit;
  Code := 1;
  if FileExists(Python) and FileExists(Script) then
    if not Exec(Python, '-I "' + Script + '" --play "' + Play + '" --work "' + Work + '" --before-delete', '',
      SW_HIDE, ewWaitUntilTerminated, Code) then
      Code := 1;
  // A silent uninstall answers No: the build folder stays.
  if (Code <> 0) and HasCardFolder(Work) then
    Result := SuppressibleMsgBox(FmtMessage(CustomMessage('UninstallSaves'), [Work]),
      mbConfirmation, MB_YESNO or MB_DEFBUTTON2, IDNO) = IDYES;
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
  if (Trim(OotfEdit.Text) <> '') and not OotfRejected then begin
    Result := Result + ' -OotfSource "' + Trim(OotfEdit.Text) + '"';
    if SubtitlePage.Values[1] then
      Result := Result + ' -OotfSubtitles generate'
    else
      Result := Result + ' -OotfSubtitles bundled';
  end else
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
  if DefaultsPage.Values[3] then
    Result := Result + ' -IssueReporting on'
  else
    Result := Result + ' -IssueReporting off';
  if HdPage.Values[0] then
    Result := Result + ' -HdTextures on'
  else
    Result := Result + ' -HdTextures auto';
  // Always passed, so "Rebuild Drakan" remembers it (build/build-options.json) and the shortcuts match.
  if CompareText(GetPlayDir(''), DefaultPlayDir) = 0 then
    Result := Result + ' -DistDirectory default'
  else
    Result := Result + ' -DistDirectory "' + GetPlayDir('') + '"';
end;
