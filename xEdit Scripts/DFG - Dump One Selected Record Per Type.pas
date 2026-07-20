unit userscript;

var
  SeenSignatures: TStringList;
  Report: TStringList;
  ReportPath: string;
  DumpedCount: Integer;
  SkippedCount: Integer;
  MirrorToMessages: Boolean;

function CleanLogText(const value: string): string;
begin
  Result := StringReplace(value, #13, '\r', [rfReplaceAll]);
  Result := StringReplace(Result, #10, '\n', [rfReplaceAll]);
  Result := StringReplace(Result, #9, '\t', [rfReplaceAll]);
  Result := StringReplace(Result, '"', '''', [rfReplaceAll]);
end;

procedure WriteLine(const value: string);
begin
  Report.Add(value);
  if MirrorToMessages then
    AddMessage(value);
end;

function SafeName(e: IInterface): string;
begin
  Result := '';
  if not Assigned(e) then
    Exit;
  try
    Result := Name(e);
  except
    Result := '<Name error>';
  end;
end;

function SafeSignature(e: IInterface): string;
begin
  Result := '';
  if not Assigned(e) then
    Exit;
  try
    Result := Signature(e);
  except
    Result := '<Signature error>';
  end;
end;

function SafeEditValue(e: IInterface): string;
begin
  Result := '';
  if not Assigned(e) then
    Exit;
  try
    Result := GetEditValue(e);
  except
    Result := '<GetEditValue error>';
  end;
end;

function SafeNativeValue(e: IInterface): string;
var
  value: Variant;
begin
  Result := '';
  if not Assigned(e) then
    Exit;
  try
    value := GetNativeValue(e);
    Result := VarToStr(value);
  except
    Result := '<GetNativeValue error>';
  end;
end;

function SafeElementType(e: IInterface): string;
begin
  Result := '';
  if not Assigned(e) then
    Exit;
  try
    Result := IntToStr(ElementType(e));
  except
    Result := '<ElementType error>';
  end;
end;

function SafeChildCount(e: IInterface): Integer;
begin
  Result := 0;
  if not Assigned(e) then
    Exit;
  try
    Result := ElementCount(e);
  except
    Result := 0;
  end;
end;

function SafeRecordFileName(e: IInterface): string;
begin
  Result := '';
  if not Assigned(e) then
    Exit;
  try
    Result := GetFileName(GetFile(e));
  except
    Result := '<GetFile error>';
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

function LinkedRecordText(e: IInterface): string;
var
  linked: IInterface;
begin
  Result := '';
  if not Assigned(e) then
    Exit;

  try
    linked := LinksTo(e);
  except
    linked := nil;
  end;

  if not Assigned(linked) then
    Exit;

  Result :=
    SafeSignature(linked) + ':' + SafeFixedFormID(linked) +
    ' file=' + SafeRecordFileName(linked) +
    ' name=' + SafeName(linked);
end;

procedure DumpElement(e: IInterface; const nodePath: string; const depth: Integer);
var
  i: Integer;
  childCount: Integer;
  child: IInterface;
  childPath: string;
  linkedText: string;
  line: string;
begin
  if not Assigned(e) then begin
    WriteLine('[DFG-DUMP] NODE depth=' + IntToStr(depth) +
      ' path="' + CleanLogText(nodePath) + '" <not assigned>');
    Exit;
  end;

  childCount := SafeChildCount(e);
  linkedText := LinkedRecordText(e);
  line :=
    '[DFG-DUMP] NODE' +
    ' depth=' + IntToStr(depth) +
    ' path="' + CleanLogText(nodePath) + '"' +
    ' name="' + CleanLogText(SafeName(e)) + '"' +
    ' signature="' + CleanLogText(SafeSignature(e)) + '"' +
    ' elementType=' + SafeElementType(e) +
    ' children=' + IntToStr(childCount) +
    ' edit="' + CleanLogText(SafeEditValue(e)) + '"' +
    ' native="' + CleanLogText(SafeNativeValue(e)) + '"';

  if linkedText <> '' then
    line := line + ' linksTo="' + CleanLogText(linkedText) + '"';

  WriteLine(line);

  for i := 0 to childCount - 1 do begin
    try
      child := ElementByIndex(e, i);
    except
      child := nil;
    end;

    childPath := nodePath + '\' + SafeName(child) + '[' + IntToStr(i) + ']';
    DumpElement(child, childPath, depth + 1);
  end;
end;

function Initialize: Integer;
var
  outputDirectory: string;
begin
  DumpedCount := 0;
  SkippedCount := 0;
  MirrorToMessages := True;

  SeenSignatures := TStringList.Create;
  SeenSignatures.Sorted := True;
  SeenSignatures.Duplicates := dupIgnore;

  Report := TStringList.Create;
  outputDirectory := wbDataPath + 'Viny Mods\Dynamic Forms Generator\Diagnostics\';
  ForceDirectories(outputDirectory);
  ReportPath := outputDirectory + 'DFG_xEdit_Selected_Record_Dump.txt';

  WriteLine('[DFG-DUMP] ==================================================');
  WriteLine('[DFG-DUMP] One selected record per signature');
  WriteLine('[DFG-DUMP] Report: ' + ReportPath);
  WriteLine('[DFG-DUMP] Select any number of records. Only the first selected record of each signature is dumped.');
  WriteLine('[DFG-DUMP] ==================================================');
  Report.SaveToFile(ReportPath);

  Result := 0;
end;

function Process(e: IInterface): Integer;
var
  sig: string;
  editorID: string;
  rootPath: string;
begin
  Result := 0;
  if not Assigned(e) then begin
    Inc(SkippedCount);
    Exit;
  end;

  sig := UpperCase(SafeSignature(e));
  if (sig = '') or (sig = 'GRUP') then begin
    Inc(SkippedCount);
    Exit;
  end;

  if SeenSignatures.IndexOf(sig) >= 0 then begin
    Inc(SkippedCount);
    Exit;
  end;
  SeenSignatures.Add(sig);

  editorID := GetElementEditValues(e, 'EDID');
  if editorID = '' then
    editorID := '<no EDID>';

  WriteLine('');
  WriteLine('[DFG-DUMP] ##################################################');
  WriteLine('[DFG-DUMP] RECORD signature=' + sig +
    ' editorID="' + CleanLogText(editorID) + '"' +
    ' formID=' + SafeFixedFormID(e) +
    ' file="' + CleanLogText(SafeRecordFileName(e)) + '"' +
    ' name="' + CleanLogText(SafeName(e)) + '"');
  WriteLine('[DFG-DUMP] ##################################################');

  rootPath := sig + ':' + SafeFixedFormID(e);
  DumpElement(e, rootPath, 0);
  Inc(DumpedCount);

  // Keep the report usable even if another selected record causes an xEdit exception.
  Report.SaveToFile(ReportPath);
end;

function Finalize: Integer;
begin
  WriteLine('');
  WriteLine('[DFG-DUMP] ==================================================');
  WriteLine('[DFG-DUMP] Done. Dumped signatures=' + IntToStr(DumpedCount) +
    ', skipped selected records=' + IntToStr(SkippedCount));
  WriteLine('[DFG-DUMP] Signatures: ' + SeenSignatures.CommaText);
  WriteLine('[DFG-DUMP] Report saved to: ' + ReportPath);
  WriteLine('[DFG-DUMP] ==================================================');

  Report.SaveToFile(ReportPath);
  SeenSignatures.Free;
  Report.Free;
  Result := 0;
end;

end.
