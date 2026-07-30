unit userscript;

var
  EditorIDs: TStringList;
  Details: TStringList;
  OutputDirectory: string;
  ListPath: string;
  DetailsPath: string;
  SelectedCount: Integer;
  AddedCount: Integer;
  DuplicateCount: Integer;
  MissingEditorIDCount: Integer;
  InvalidSelectionCount: Integer;

function SafeSignature(e: IInterface): string;
begin
  Result := '';
  if not Assigned(e) then
    Exit;
  try
    Result := Signature(e);
  except
    Result := '';
  end;
end;

function SafeEditorID(e: IInterface): string;
begin
  Result := '';
  if not Assigned(e) then
    Exit;
  try
    Result := Trim(GetElementEditValues(e, 'EDID'));
  except
    Result := '';
  end;
end;

function SafeFileName(e: IInterface): string;
begin
  Result := '';
  if not Assigned(e) then
    Exit;
  try
    Result := GetFileName(GetFile(e));
  except
    Result := '<file error>';
  end;
end;

function SafeFixedFormID(e: IInterface): string;
begin
  Result := '';
  if not Assigned(e) then
    Exit;
  try
    Result := IntToHex(FixedFormID(e), 8);
  except
    Result := '<FormID error>';
  end;
end;

function IsMainRecord(e: IInterface): Boolean;
begin
  Result := False;
  if not Assigned(e) then
    Exit;
  try
    Result := ElementType(e) = etMainRecord;
  except
    Result := False;
  end;
end;

function ResolveSelectedRecord(e: IInterface): IInterface;
var
  current: IInterface;
  linked: IInterface;
begin
  Result := nil;
  if not Assigned(e) then
    Exit;

  linked := nil;
  try
    linked := LinksTo(e);
  except
    linked := nil;
  end;

  if Assigned(linked) then begin
    current := linked;
    while Assigned(current) do begin
      if IsMainRecord(current) then begin
        Result := current;
        Exit;
      end;
      try
        current := Container(current);
      except
        current := nil;
      end;
    end;
  end;

  current := e;
  while Assigned(current) do begin
    if IsMainRecord(current) then begin
      Result := current;
      Exit;
    end;
    try
      current := Container(current);
    except
      current := nil;
    end;
  end;
end;

function Initialize: Integer;
begin
  SelectedCount := 0;
  AddedCount := 0;
  DuplicateCount := 0;
  MissingEditorIDCount := 0;
  InvalidSelectionCount := 0;

  EditorIDs := TStringList.Create;
  EditorIDs.Sorted := True;
  EditorIDs.Duplicates := dupIgnore;

  Details := TStringList.Create;
  Details.Add('EditorID' + #9 + 'Signature' + #9 + 'FormID' + #9 + 'Plugin');

  OutputDirectory := wbDataPath + 'Viny Mods\Dynamic Forms Generator\Diagnostics\';
  ForceDirectories(OutputDirectory);
  ListPath := OutputDirectory + 'DFG_xEdit_Selected_EditorIDs.txt';
  DetailsPath := OutputDirectory + 'DFG_xEdit_Selected_EditorIDs_Details.tsv';

  AddMessage('[DFG-EDID] Select records or FormID reference elements, then apply this script.');
  AddMessage('[DFG-EDID] EditorID list: ' + ListPath);
  AddMessage('[DFG-EDID] Details: ' + DetailsPath);
  Result := 0;
end;

function Process(e: IInterface): Integer;
var
  recordElement: IInterface;
  editorID: string;
  signatureText: string;
  existingIndex: Integer;
begin
  Result := 0;
  Inc(SelectedCount);

  recordElement := ResolveSelectedRecord(e);
  if not Assigned(recordElement) then begin
    Inc(InvalidSelectionCount);
    AddMessage('[DFG-EDID] Skipped selection: no main record or linked record was found.');
    Exit;
  end;

  signatureText := UpperCase(SafeSignature(recordElement));
  if (signatureText = '') or (signatureText = 'GRUP') then begin
    Inc(InvalidSelectionCount);
    AddMessage('[DFG-EDID] Skipped invalid record selection.');
    Exit;
  end;

  editorID := SafeEditorID(recordElement);
  if editorID = '' then begin
    Inc(MissingEditorIDCount);
    AddMessage(
      '[DFG-EDID] Skipped ' + signatureText + ':' + SafeFixedFormID(recordElement) +
      ' from ' + SafeFileName(recordElement) + ': record has no EditorID.');
    Exit;
  end;

  existingIndex := EditorIDs.IndexOf(editorID);
  if existingIndex >= 0 then begin
    Inc(DuplicateCount);
    Exit;
  end;

  EditorIDs.Add(editorID);
  Details.Add(
    editorID + #9 +
    signatureText + #9 +
    SafeFixedFormID(recordElement) + #9 +
    SafeFileName(recordElement));
  Inc(AddedCount);
end;

function Finalize: Integer;
begin
  EditorIDs.SaveToFile(ListPath);
  Details.SaveToFile(DetailsPath);

  AddMessage(
    '[DFG-EDID] Done. Selected=' + IntToStr(SelectedCount) +
    ', listed=' + IntToStr(AddedCount) +
    ', duplicates=' + IntToStr(DuplicateCount) +
    ', without EditorID=' + IntToStr(MissingEditorIDCount) +
    ', invalid=' + IntToStr(InvalidSelectionCount) + '.');
  AddMessage('[DFG-EDID] EditorID list saved to: ' + ListPath);
  AddMessage('[DFG-EDID] Details saved to: ' + DetailsPath);

  EditorIDs.Free;
  Details.Free;
  Result := 0;
end;

end.
