; QGit installation script for Inno Setup compiler

[Setup]
AppName=QGit
AppVerName=QGit version 2.1
DefaultDirName={pf}\QGit
DefaultGroupName=QGit
UninstallDisplayIcon={app}\qgit.exe
Compression=lzma
SolidCompression=yes
LicenseFile=COPYING.txt
SetupIconFile=qgit.ico
SourceDir=C:\Users\Marco\Documenti\qgit\bin
OutputDir=.
OutputBaseFilename=qgit2.1_win.exe

[Files]
Source: "qgit.exe"; DestDir: "{app}"
Source: "start_qgit.bat"; DestDir: "{app}"; AfterInstall: UpdateMsysGitPath;
Source: "qgit.exe.manifest"; DestDir: "{app}"
Source: "Microsoft.VC90.CRT\*"; DestDir: "{app}\Microsoft.VC90.CRT"
Source: "README.txt"; DestDir: "{app}"; Flags: isreadme

[Registry]
Root: HKCU; Subkey: "Software\qgit"; Flags: uninsdeletekeyifempty
Root: HKCU; Subkey: "Software\qgit\qgit4"; ValueType: string; ValueName: "msysgit_exec_dir"; ValueData: "{code:GetMSysGitExecDir}";

[Dirs]
Name: {code:GetMSysGitExecDir}; Flags: uninsneveruninstall

[Tasks]
Name: desktopicon; Description: "Create a &desktop icon";

[Icons]
Name: "{group}\QGit"; Filename: "{app}\start_qgit.bat"; IconFilename: "{app}\qgit.exe"; WorkingDir: "{app}"
Name: "{group}\Uninstall QGit"; Filename: "{uninstallexe}"
Name: "{commondesktop}\QGit"; Filename: "{app}\start_qgit.bat"; IconFilename: "{app}\qgit.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Code]
var
  MSysGitDirPage: TInputDirWizardPage;
  
procedure InitializeWizard;
begin
  // Create msysgit directory find page
  MSysGitDirPage := CreateInputDirPage(wpSelectProgramGroup,
      'Select MSYSGIT Location', 'Where is MSYSGIT directory located?',
      'Select where MSYSGIT directory is located, then click Next.',
      False, '');
  
  // Add item (with an empty caption)
  MSysGitDirPage.Add('');
  
  // Set initial value
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

// called after install to update start_qgit.bat with correct msysgit path
procedure UpdateMsysGitPath();
var
  Buf: String;
  NewPath: String;
  OldPath: String;
  s1: Integer;
  s2: Integer;
begin
  NewPath := 'SET MSYSGIT_EXEC_DIR=' + GetMSysGitExecDir('dummy');
  LoadStringFromFile(ExpandConstant('{app}\start_qgit.bat'), Buf);
  s1 := Pos('SET MSYSGIT_EXEC_DIR=', Buf);
  if s1 > 0 then begin
      s2 := Pos('SET PATH=%PATH%;', Buf);
      if s2 - 4 > s1 then begin // count some line feeds
          OldPath := Copy(Buf, s1, s2 - 4 - s1);
          StringChangeEx(Buf, OldPath, NewPath, true);
          SaveStringToFile(ExpandConstant('{app}\start_qgit.bat'), Buf, False);
    end;
  end;
end;

