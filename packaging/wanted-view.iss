#ifndef StageDir
  #error StageDir must point to the cmake --install output
#endif
#ifndef ProductVersion
  #error ProductVersion must match the application version
#endif

[Setup]
AppId=Kadro
AppName=Wanted View
AppVersion={#ProductVersion}
AppPublisher=WANTED Labs
DefaultDirName={localappdata}\Programs\Kadro
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64os
ArchitecturesInstallIn64BitMode=x64os
OutputBaseFilename=WantedView-Windows-x64-RC
SetupIconFile=icons\wanted-view.ico
UninstallDisplayName=Wanted View
UninstallDisplayIcon={app}\bin\Kadro.exe
WizardStyle=modern
ChangesAssociations=yes

[Languages]
Name: "russian"; MessagesFile: "compiler:Languages\Russian.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion

[Icons]
Name: "{userprograms}\Wanted View"; Filename: "{app}\bin\Kadro.exe"; WorkingDir: "{app}\bin"

[Code]
const
  Classes = 'Software\Classes\';
  ProgIdKey = 'Kadro.Media';
  ApplicationKey = 'Applications\Kadro.exe';
  Capabilities = 'Software\Kadro\Capabilities';
  RegisteredApps = 'Software\RegisteredApplications';

function HasCommand(Key, Expected: String): Boolean;
var
  Command: String;
begin
  Result := RegQueryStringValue(HKCU, Classes + Key + '\shell\open\command', '', Command)
    and (CompareText(Command, Expected) = 0);
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  Expected, Association, Ext: String;
  Extensions: TArrayOfString;
  I: Integer;
begin
  if CurUninstallStep <> usUninstall then
    Exit;

  Expected := '"' + ExpandConstant('{app}\bin\Kadro.exe') + '" "%1"';
  { Both commands must belong to this copy. A newer or portable copy may own them. }
  if not (HasCommand(ProgIdKey, Expected) and HasCommand(ApplicationKey, Expected)) then
    Exit;

  if RegGetValueNames(HKCU, Capabilities + '\FileAssociations', Extensions) then
    for I := 0 to GetArrayLength(Extensions) - 1 do
    begin
      Ext := Extensions[I];
      if (Copy(Ext, 1, 1) = '.') and (Pos('\', Ext) = 0)
        and RegQueryStringValue(HKCU, Capabilities + '\FileAssociations', Ext, Association)
        and (Association = ProgIdKey) then
        RegDeleteValue(HKCU, Classes + Ext + '\OpenWithProgids', ProgIdKey);
    end;

  RegDeleteKeyIncludingSubkeys(HKCU, Classes + ProgIdKey);
  RegDeleteKeyIncludingSubkeys(HKCU, Classes + ApplicationKey);
  RegDeleteKeyIncludingSubkeys(HKCU, Capabilities);
  if RegQueryStringValue(HKCU, RegisteredApps, 'KADRO', Association) and (Association = Capabilities) then
    RegDeleteValue(HKCU, RegisteredApps, 'KADRO');
end;
