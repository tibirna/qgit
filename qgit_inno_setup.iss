; QGit installation script for Inno Setup compiler
;
; QGit should be compiled with MSVC 2008, statically linked
; to Qt4.3 or better Trolltech libraries and directory of
; Visual C++ redistributable dll files copied under 'bin\'
; directory

[Setup]
AppName=QGit
AppVerName=QGit version 2.11
DefaultDirName={pf}\QGit
DefaultGroupName=QGit
UninstallDisplayIcon={app}\qgit.exe
Compression=lzma
SolidCompression=yes
LicenseFile=COPYING.rtf
SetupIconFile=src\resources\qgit.ico
OutputDir=bin
OutputBaseFilename=qgit-2.3_win

[Files]
Source: "bin\qgit.exe"; DestDir: "{app}"
Source: "bin\qgit.exe.manifest"; DestDir: "{app}"
Source: "README_WIN.txt"; DestDir: "{app}"; Flags: isreadme

; Directory of MSVC redistributable files must be copied under 'bin\'
; before to run Inno Setup compiler or following line will error out
Source: "bin\Microsoft.VC90.CRT\*"; DestDir: "{app}\Microsoft.VC90.CRT"

[Tasks]
Name: desktopicon; Description: "Create a &desktop icon";
Name: winexplorer; Description: "Add ""QGit Here"" in Windows Explorer context menu";

[Registry]
Root: HKCU; Subkey: "Software\qgit"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\qgit\qgit4"; ValueType: string; ValueName: "msysgit_exec_dir"; ValueData: "{code:GetMSysGitExecDir}";

; Windows Explorer integration
Root: HKCU; Subkey: "SOFTWARE\Classes\Directory\shell\qgit"; Flags: uninsdeletekey; Tasks: winexplorer
Root: HKCU; Subkey: "SOFTWARE\Classes\Directory\shell\qgit"; ValueType: string; ValueName: ""; ValueData: "&QGit Here"; Tasks: winexplorer
Root: HKCU; Subkey: "SOFTWARE\Classes\Directory\shell\qgit\command"; ValueType: string; ValueName: ""; ValueData: "{app}\qgit.exe"; Tasks: winexplorer

[Dirs]
Name: {code:GetMSysGitExecDir}; Flags: uninsneveruninstall

[Icons]
Name: "{group}\QGit"; Filename: "{app}\qgit.exe"; WorkingDir: "%USERPROFILE%";
Name: "{group}\Uninstall QGit"; Filename: "{uninstallexe}"
Name: "{commondesktop}\QGit"; Filename: "{app}\qgit.exe"; WorkingDir: "%USERPROFILE%"; Tasks: desktopicon

[Code]
var
  MSysGitDirPage: TInputDirWizardPage;

procedure InitializeWizard;
var
  Key, Val: String;

begin
  // Create msysgit directory find page
  MSysGitDirPage := CreateInputDirPage(wpSelectProgramGroup,
      'Select MSYSGIT Location', 'Where is MSYSGIT directory located?',
      'Select where MSYSGIT directory is located, then click Next.',
      False, '');

  // Add item (with an empty caption)
  MSysGitDirPage.Add('');

  // Set initial value
  Key := 'SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Git_is1';

  if RegQueryStringValue(HKEY_LOCAL_MACHINE, Key, 'InstallLocation', Val) then begin
    MSysGitDirPage.Values[0] := Val;
  end else
    MSysGitDirPage.Values[0] := ExpandConstant('{pf}\Git');

end;

function NextButtonClick(CurPageID: Integer): Boolean;
var
  BaseDir: String;

begin
  // Validate pages before allowing the user to proceed }
  if CurPageID = MSysGitDirPage.ID then begin

      BaseDir := MSysGitDirPage.Values[0];

      if FileExists(ExpandFileName(BaseDir + '\bin\git.exe')) then begin
        Result := True;

      end else if FileExists(ExpandFileName(BaseDir + '\..\bin\git.exe')) then begin // sub dir selected
        MSysGitDirPage.Values[0] := ExpandFileName(BaseDir + '\..');
        Result := True;

      end else begin
        MsgBox('Directory ''' + BaseDir + ''' does not seem the msysgit one, retry', mbError, MB_OK);
        Result := False;
      end;

  end else
    Result := True;
end;

function GetMSysGitExecDir(Param: String): String;
begin
  Result := MSysGitDirPage.Values[0] + '\bin'; // already validated
end;
