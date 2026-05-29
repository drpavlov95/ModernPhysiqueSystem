unit userscript;

uses
  SysUtils;

const
  TargetPluginName = 'RleyBodyMorphs.esp';
  TextureSetPrefix = 'RBM_TX_';
  NormalRoot = 'RleyBodyMorphs\';
  DiffusePath = '!COR\Body\femalebody_1.dds';
  GlowDetailPath = '!COR\Body\femalebody_1_sk.dds';
  BacklightSpecularPath = '!COR\Body\femalebody_1_s.dds';

var
  TargetFile: IInterface;

function FindLoadedFileByName(const TargetName: string): IInterface;
var
  i: Integer;
  Candidate: IInterface;
begin
  Result := nil;
  for i := 0 to FileCount - 1 do begin
    Candidate := FileByIndex(i);
    if SameText(GetFileName(Candidate), TargetName) then begin
      Result := Candidate;
      Exit;
    end;
  end;
end;

function GetGroupBySignatureOrCreate(aFile: IInterface; const Signature: string): IInterface;
begin
  Result := GroupBySignature(aFile, Signature);
  if not Assigned(Result) then
    Result := Add(aFile, Signature, True);
end;

function GetRecordEditorID(aRecord: IInterface): string;
var
  EdidElement: IInterface;
begin
  Result := '';
  EdidElement := ElementBySignature(aRecord, 'EDID');
  if Assigned(EdidElement) then
    Result := GetEditValue(EdidElement);
end;

function FindRecordByEditorIDInFile(aFile: IInterface; const Signature, TargetEditorID: string): IInterface;
var
  Group, Rec: IInterface;
  i: Integer;
begin
  Result := nil;
  Group := GroupBySignature(aFile, Signature);
  if not Assigned(Group) then
    Exit;

  for i := 0 to ElementCount(Group) - 1 do begin
    Rec := ElementByIndex(Group, i);
    if SameText(GetRecordEditorID(Rec), TargetEditorID) then begin
      Result := Rec;
      Exit;
    end;
  end;
end;

function EnsureSubrecord(aRecord: IInterface; const Signature: string): IInterface;
begin
  Result := ElementBySignature(aRecord, Signature);
  if Assigned(Result) then
    Exit;

  Add(aRecord, Signature, True);
  Result := ElementBySignature(aRecord, Signature);
  if not Assigned(Result) then
    raise Exception.Create('Failed to add subrecord ' + Signature);
end;

function EnsureTexturesBlock(aRecord: IInterface): IInterface;
begin
  Result := ElementByPath(aRecord, 'Textures (RGB/A)');
  if Assigned(Result) then
    Exit;

  Result := Add(aRecord, 'Textures (RGB/A)', True);
  if not Assigned(Result) then
    Result := ElementByPath(aRecord, 'Textures (RGB/A)');
  if not Assigned(Result) then
    raise Exception.Create('Failed to add Textures (RGB/A) block');
end;

procedure SetTextureValue(aRecord: IInterface; const Signature, Value: string);
var
  TexturesBlock: IInterface;
  TextureField: IInterface;
begin
  TexturesBlock := EnsureTexturesBlock(aRecord);
  TextureField := ElementBySignature(TexturesBlock, Signature);
  if not Assigned(TextureField) then begin
    Add(TexturesBlock, Signature, True);
    TextureField := ElementBySignature(TexturesBlock, Signature);
  end;
  if not Assigned(TextureField) then
    raise Exception.Create('Failed to locate texture field ' + Signature);
  SetEditValue(TextureField, Value);
end;

procedure SetSubrecordValue(aRecord: IInterface; const Signature, Value: string);
var
  Subrecord: IInterface;
begin
  Subrecord := EnsureSubrecord(aRecord, Signature);
  SetEditValue(Subrecord, Value);
end;

procedure SetDNAMFlags(aRecord: IInterface);
var
  Dnam: IInterface;
begin
  Dnam := EnsureSubrecord(aRecord, 'DNAM');
  SetNativeValue(Dnam, 6);
end;

procedure CreateOne(const FileStem: string);
var
  EditorIDValue: string;
  Group, Existing, Rec: IInterface;
begin
  EditorIDValue := TextureSetPrefix + FileStem;
  Existing := FindRecordByEditorIDInFile(TargetFile, 'TXST', EditorIDValue);
  if Assigned(Existing) then
    Remove(Existing);

  Group := GetGroupBySignatureOrCreate(TargetFile, 'TXST');
  Rec := Add(Group, 'TXST', True);
  if not Assigned(Rec) then
    raise Exception.Create('Failed to create ' + EditorIDValue);

  SetSubrecordValue(Rec, 'EDID', EditorIDValue);
  SetTextureValue(Rec, 'TX00', DiffusePath);
  SetTextureValue(Rec, 'TX01', NormalRoot + FileStem + '.dds');
  SetTextureValue(Rec, 'TX03', GlowDetailPath);
  SetTextureValue(Rec, 'TX07', BacklightSpecularPath);
  SetDNAMFlags(Rec);
  AddMessage('Created ' + EditorIDValue);
end;

function Initialize: Integer;
begin
  Result := 0;

  TargetFile := FindLoadedFileByName(TargetPluginName);
  if not Assigned(TargetFile) then begin
    AddMessage('ERROR: Target plugin not loaded: ' + TargetPluginName);
    Result := 1;
    Exit;
  end;

  AddMessage('Creating TXSTs in ' + TargetPluginName);

  CreateOne('H0_M0_T0');
  CreateOne('H0_M0_T10');
  CreateOne('H0_M0_T100');
  CreateOne('H0_M0_T25');
  CreateOne('H0_M0_T50');
  CreateOne('H0_M0_T75');
  CreateOne('H0_M100_T0');
  CreateOne('H0_M25_TX');
  CreateOne('H0_M50_TX');
  CreateOne('H0_M75_TX');
  CreateOne('H10_M10_TX');
  CreateOne('H100_M0_T0');
  CreateOne('H25_M0_TX');
  CreateOne('H25_M25_T0');
  CreateOne('H25_M50_TX');
  CreateOne('H25_M75_TX');
  CreateOne('H50_M0_TX');
  CreateOne('H50_M25_T0');
  CreateOne('H50_M50_T0');
  CreateOne('H50_M75_TX');
  CreateOne('H75_M0_TX');
  CreateOne('H75_M25_TX');
  CreateOne('H75_M50_TX');

  AddMessage('Done.');
end;

function Finalize: Integer;
begin
  Result := 0;
end;

end.
