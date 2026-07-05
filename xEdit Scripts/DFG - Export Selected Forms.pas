unit userscript;

var
  OutputDir: string;
  ExportedCount: Integer;
  SkippedCount: Integer;

  // NPC flag diagnostics.
  // Leave DebugNpcEditorID empty to log every selected NPC.
  DebugNpcFlags: Boolean;
  DebugNpcEditorID: string;

function EndsWith(const s: string; const suffix: string): Boolean;
begin
  Result := Copy(s, Length(s) - Length(suffix) + 1, Length(suffix)) = suffix;
end;

procedure RemoveTrailingComma(sl: TStringList);
var
  i: Integer;
  s: string;
begin
  for i := sl.Count - 1 downto 0 do begin
    s := sl[i];
    if s = '' then
      Continue;
    if EndsWith(s, ',') then
      sl[i] := Copy(s, 1, Length(s) - 1);
    Exit;
  end;
end;

function JsonEscape(const s: string): string;
var
  i: Integer;
  c: string;
begin
  Result := '';
  for i := 1 to Length(s) do begin
    c := Copy(s, i, 1);
    if c = '\' then
      Result := Result + '\\'
    else if c = '"' then
      Result := Result + '\"'
    else if c = #8 then
      Result := Result + '\b'
    else if c = #9 then
      Result := Result + '\t'
    else if c = #10 then
      Result := Result + '\n'
    else if c = #12 then
      Result := Result + '\f'
    else if c = #13 then
      Result := Result + '\r'
    else
      Result := Result + c;
  end;
end;

function JStr(const s: string): string;
begin
  Result := '"' + JsonEscape(s) + '"';
end;

function SafeFloat(const s: string; fallback: string): string;
begin
  Result := StringReplace(Trim(s), ',', '.', [rfReplaceAll]);
  if Result = '' then
    Result := fallback;
end;

function JsonFloat(const s: string; fallback: string): string;
var
  i, dotCount: Integer;
  c: string;
begin
  Result := StringReplace(Trim(s), ',', '.', [rfReplaceAll]);
  if Result = '' then begin
    Result := fallback;
    Exit;
  end;

  dotCount := 0;
  for i := 1 to Length(Result) do begin
    c := Copy(Result, i, 1);
    if (i = 1) and (c = '-') then
      Continue;
    if c = '.' then begin
      Inc(dotCount);
      if dotCount > 1 then begin
        Result := fallback;
        Exit;
      end;
      Continue;
    end;
    if (c < '0') or (c > '9') then begin
      Result := fallback;
      Exit;
    end;
  end;
end;

function SafeInt(const s: string; fallback: string): string;
begin
  Result := Trim(s);
  if Result = '' then
    Result := fallback;
end;

function JsonInt(const s: string; fallback: string): string;
var
  i: Integer;
  c: string;
begin
  Result := Trim(s);
  if Result = '' then begin
    Result := fallback;
    Exit;
  end;

  for i := 1 to Length(Result) do begin
    c := Copy(Result, i, 1);
    if (i = 1) and (c = '-') then
      Continue;
    if (c < '0') or (c > '9') then begin
      Result := fallback;
      Exit;
    end;
  end;
end;

function IsSafeFileChar(const c: string): Boolean;
begin
  Result :=
    ((c >= 'A') and (c <= 'Z')) or
    ((c >= 'a') and (c <= 'z')) or
    ((c >= '0') and (c <= '9')) or
    (c = '_');
end;

function BoolJson(b: Boolean): string;
begin
  if b then
    Result := 'true'
  else
    Result := 'false';
end;

procedure AddKV(sl: TStringList; indent, key, value: string);
begin
  sl.Add(indent + JStr(key) + ': ' + value + ',');
end;

procedure AddStringKV(sl: TStringList; indent, key, value: string);
begin
  AddKV(sl, indent, key, JStr(value));
end;

function EditorIDOf(e: IInterface): string;
begin
  Result := GetElementEditValues(e, 'EDID');
  if Result = '' then
    Result := Signature(e) + '_' + IntToHex(FixedFormID(e), 8);
end;

function SafeFileName(const s: string): string;
var
  i: Integer;
  c: string;
begin
  Result := '';
  for i := 1 to Length(s) do begin
    c := Copy(s, i, 1);
    if IsSafeFileChar(c) then
      Result := Result + c
    else
      Result := Result + '_';
  end;
end;

function PluginLocalID(e: IInterface): string;
begin
  if not Assigned(e) then begin
    Result := '';
    Exit;
  end;
  Result := GetFileName(GetFile(e)) + '|' + IntToHex(FixedFormID(e), 1);
end;

function LinkedByPath(e: IInterface; path: string): IInterface;
var
  el: IInterface;
begin
  Result := nil;
  el := ElementByPath(e, path);
  if Assigned(el) then
    Result := LinksTo(el);
end;

function FirstElementByPath(e: IInterface; path1, path2, path3, path4: string): IInterface;
begin
  Result := nil;
  if Assigned(e) and (path1 <> '') then
    Result := ElementByPath(e, path1);
  if (not Assigned(Result)) and Assigned(e) and (path2 <> '') then
    Result := ElementByPath(e, path2);
  if (not Assigned(Result)) and Assigned(e) and (path3 <> '') then
    Result := ElementByPath(e, path3);
  if (not Assigned(Result)) and Assigned(e) and (path4 <> '') then
    Result := ElementByPath(e, path4);
end;

function FirstLinkedInElement(e: IInterface): IInterface;
var
  i: Integer;
  child: IInterface;
begin
  Result := nil;
  if not Assigned(e) then
    Exit;

  Result := LinksTo(e);
  if Assigned(Result) then
    Exit;

  for i := 0 to ElementCount(e) - 1 do begin
    child := FirstLinkedInElement(ElementByIndex(e, i));
    if Assigned(child) then begin
      Result := child;
      Exit;
    end;
  end;
end;

function FindChildBySignature(e: IInterface; sig: string): IInterface;
var
  i: Integer;
  child: IInterface;
begin
  Result := nil;
  if not Assigned(e) then
    Exit;

  for i := 0 to ElementCount(e) - 1 do begin
    child := ElementByIndex(e, i);
    if Signature(child) = sig then begin
      Result := child;
      Exit;
    end;
  end;
end;

function FindChildByNameWord(e: IInterface; word: string): IInterface;
var
  i: Integer;
  child: IInterface;
begin
  Result := nil;
  if not Assigned(e) then
    Exit;

  for i := 0 to ElementCount(e) - 1 do begin
    child := ElementByIndex(e, i);
    if Pos(LowerCase(word), LowerCase(Name(child))) > 0 then begin
      Result := child;
      Exit;
    end;
  end;
end;

function GetFirstText(e: IInterface; path1, path2, path3, path4, fallback: string): string;
begin
  Result := '';
  if Assigned(e) and (path1 <> '') then
    Result := GetElementEditValues(e, path1);
  if (Result = '') and Assigned(e) and (path2 <> '') then
    Result := GetElementEditValues(e, path2);
  if (Result = '') and Assigned(e) and (path3 <> '') then
    Result := GetElementEditValues(e, path3);
  if (Result = '') and Assigned(e) and (path4 <> '') then
    Result := GetElementEditValues(e, path4);
  if Result = '' then
    Result := fallback;
end;

function EditValueOf(e: IInterface): string;
begin
  Result := '';
  if Assigned(e) then
    Result := GetEditValue(e);
end;

function ElementTextRecursive(e: IInterface): string;
var
  i: Integer;
  childText, editValue: string;
begin
  Result := '';
  if not Assigned(e) then
    Exit;

  Result := Name(e);
  editValue := GetEditValue(e);
  if editValue <> '' then
    Result := Result + ' ' + editValue;

  for i := 0 to ElementCount(e) - 1 do begin
    childText := ElementTextRecursive(ElementByIndex(e, i));
    if childText <> '' then
      Result := Result + ' ' + childText;
  end;
end;

function FindChildByNameContains(e: IInterface; text: string): IInterface;
var
  i: Integer;
  child: IInterface;
begin
  Result := nil;
  if not Assigned(e) then
    Exit;

  for i := 0 to ElementCount(e) - 1 do begin
    child := ElementByIndex(e, i);
    if Pos(LowerCase(text), LowerCase(Name(child))) > 0 then begin
      Result := child;
      Exit;
    end;
  end;
end;


function DebugBoolText(value: Boolean): string;
begin
  if value then
    Result := 'true'
  else
    Result := 'false';
end;

function ShouldDebugNpc(e: IInterface): Boolean;
begin
  Result := DebugNpcFlags and Assigned(e);
  if not Result then
    Exit;

  if DebugNpcEditorID <> '' then
    Result := LowerCase(EditorIDOf(e)) = LowerCase(DebugNpcEditorID);
end;

procedure DebugDumpElement(e: IInterface; indent: string; depth: Integer);
var
  i: Integer;
  child: IInterface;
  editValue: string;
begin
  if not Assigned(e) then begin
    AddMessage('[DFG][FLAGS] ' + indent + '<element not assigned>');
    Exit;
  end;

  editValue := GetEditValue(e);

  AddMessage(
    '[DFG][FLAGS] ' + indent +
    'Name="' + Name(e) + '"' +
    ' Signature="' + Signature(e) + '"' +
    ' EditValue="' + editValue + '"' +
    ' Children=' + IntToStr(ElementCount(e))
  );

  if depth <= 0 then
    Exit;

  for i := 0 to ElementCount(e) - 1 do begin
    child := ElementByIndex(e, i);
    DebugDumpElement(child, indent + '  ', depth - 1);
  end;
end;

procedure DebugNpcFlagPath(e: IInterface; path: string);
var
  el: IInterface;
  editValue: string;
  nativeValue: Integer;
begin
  el := ElementByPath(e, path);
  editValue := GetElementEditValues(e, path);
  nativeValue := GetElementNativeValues(e, path);

  AddMessage(
    '[DFG][FLAGS] Path="' + path + '"' +
    ' Assigned=' + DebugBoolText(Assigned(el)) +
    ' EditValue="' + editValue + '"' +
    ' NativeValue=' + IntToStr(nativeValue) +
    ' Hex=0x' + IntToHex(nativeValue, 8)
  );

  if Assigned(el) then
    DebugDumpElement(el, '  ', 2);
end;

procedure DebugNpcFlagsRecord(e: IInterface; parsedFlags: string);
var
  acbs: IInterface;
begin
  if not ShouldDebugNpc(e) then
    Exit;

  AddMessage('[DFG][FLAGS] ==================================================');
  AddMessage('[DFG][FLAGS] NPC: ' + Name(e));
  AddMessage('[DFG][FLAGS] EditorID: ' + EditorIDOf(e));
  AddMessage('[DFG][FLAGS] Parsed flags text: "' + parsedFlags + '"');

  // Test the common internal and display paths separately.
  DebugNpcFlagPath(e, 'ACBS\Flags');
  DebugNpcFlagPath(e, 'ACBS - Configuration\Flags');
  DebugNpcFlagPath(e, 'ACBS\Flags (sorted)');
  DebugNpcFlagPath(e, 'ACBS - Configuration\Flags (sorted)');

  acbs := ElementByPath(e, 'ACBS');
  AddMessage('[DFG][FLAGS] ElementByPath("ACBS") assigned=' +
    DebugBoolText(Assigned(acbs)));
  if Assigned(acbs) then
    DebugDumpElement(acbs, '  ', 3);

  acbs := ElementByPath(e, 'ACBS - Configuration');
  AddMessage('[DFG][FLAGS] ElementByPath("ACBS - Configuration") assigned=' +
    DebugBoolText(Assigned(acbs)));
  if Assigned(acbs) then
    DebugDumpElement(acbs, '  ', 3);

  // The record dump reveals the exact top-level names exposed by this xEdit build.
  AddMessage('[DFG][FLAGS] Top-level NPC element dump:');
  DebugDumpElement(e, '  ', 2);
end;

function NpcFlagsText(e: IInterface): string;
var
  config, flagsElement: IInterface;
begin
  Result := '';

  // ElementByPath/GetElementEditValues use the internal xEdit path.
  // "ACBS - Configuration" and "Flags (sorted)" are presentation labels
  // shown by the UI and are not reliable script paths.
  Result := GetElementEditValues(e, 'ACBS\Flags');
  if Result <> '' then
    Exit;

  flagsElement := ElementByPath(e, 'ACBS\Flags');
  if Assigned(flagsElement) then
    Result := ElementTextRecursive(flagsElement);
  if Result <> '' then
    Exit;

  // Fallback for xEdit versions/definitions that expose the flag node
  // differently.
  config := ElementByPath(e, 'ACBS');
  if Assigned(config) then begin
    flagsElement := FindChildByNameContains(config, 'Flags');
    if Assigned(flagsElement) then
      Result := ElementTextRecursive(flagsElement);

    if Result = '' then
      Result := ElementTextRecursive(config);
  end;
end;


function FlagTextIsEnabled(const value: string): Boolean;
var
  normalized: string;
begin
  normalized := LowerCase(Trim(value));

  Result :=
    (normalized = '1') or
    (normalized = 'true') or
    (normalized = 'yes') or
    (normalized = 'set') or
    (normalized = 'enabled');
end;

function NpcFlagEnabled(e: IInterface; const flagName: string): Boolean;
var
  flagsElement, child: IInterface;
  i: Integer;
  childName, childValue: string;
begin
  Result := False;

  flagsElement := ElementByPath(e, 'ACBS\Flags');
  if not Assigned(flagsElement) then
    flagsElement := ElementByPath(e, 'ACBS - Configuration\Flags');

  if not Assigned(flagsElement) then begin
    if ShouldDebugNpc(e) then
      AddMessage('[DFG][FLAGS] Flag container not found while reading "' +
        flagName + '"');
    Exit;
  end;

  for i := 0 to ElementCount(flagsElement) - 1 do begin
    child := ElementByIndex(flagsElement, i);
    if not Assigned(child) then
      Continue;

    childName := Name(child);
    if LowerCase(childName) <> LowerCase(flagName) then
      Continue;

    childValue := GetEditValue(child);
    Result := FlagTextIsEnabled(childValue);

    if ShouldDebugNpc(e) then
      AddMessage(
        '[DFG][FLAGS] Direct flag "' + flagName + '"' +
        ' found=true' +
        ' value="' + childValue + '"' +
        ' enabled=' + DebugBoolText(Result)
      );

    Exit;
  end;

  if ShouldDebugNpc(e) then
    AddMessage(
      '[DFG][FLAGS] Direct flag "' + flagName + '"' +
      ' found=false enabled=false'
    );
end;

function NpcFlagEnabledEither(
  e: IInterface;
  const firstName, secondName: string
): Boolean;
begin
  Result :=
    NpcFlagEnabled(e, firstName) or
    NpcFlagEnabled(e, secondName);
end;

function FormRefJson(r: IInterface): string;
var
  edid, fid: string;
begin
  if not Assigned(r) then begin
    Result := '{}';
    Exit;
  end;

  edid := EditorIDOf(r);
  fid := PluginLocalID(r);
  Result := '{ "editorID": ' + JStr(edid) + ', "formID": ' + JStr(fid) + ' }';
end;

procedure AddFormRefKV(sl: TStringList; key: string; r: IInterface);
begin
  if Assigned(r) then
    AddKV(sl, '  ', key, FormRefJson(r));
end;

function GetText(e: IInterface; path: string): string;
begin
  Result := GetElementEditValues(e, path);
end;

function HasWord(const s: string; const word: string): Boolean;
begin
  Result := Pos(LowerCase(word), LowerCase(s)) > 0;
end;

function FirstText(e: IInterface; path1, path2, path3: string): string;
begin
  Result := GetElementEditValues(e, path1);
  if (Result = '') and (path2 <> '') then
    Result := GetElementEditValues(e, path2);
  if (Result = '') and (path3 <> '') then
    Result := GetElementEditValues(e, path3);
end;

function ColorChannel(e: IInterface; path: string; fallback: string): string;
begin
  Result := SafeInt(GetText(e, path), fallback);
end;

function FormKindForSignature(sig: string): string;
begin
  if sig = 'GLOB' then Result := 'Global'
  else if sig = 'KYWD' then Result := 'Keyword'
  else if sig = 'OTFT' then Result := 'Outfit'
  else if sig = 'ARMA' then Result := 'ArmorType'
  else if sig = 'ARMO' then Result := 'Armor'
  else if sig = 'CLFM' then Result := 'Color'
  else if sig = 'ARTO' then Result := 'ArtObject'
  else if sig = 'PERK' then Result := 'Perk'
  else if sig = 'HDPT' then Result := 'HeadPart'
  else if sig = 'SNDR' then Result := 'SoundDescriptor'
  else if sig = 'LIGH' then Result := 'Light'
  else if sig = 'EXPL' then Result := 'Explosion'
  else if sig = 'ACTI' then Result := 'Activator'
  else if sig = 'EFSH' then Result := 'EffectShader'
  else if sig = 'NPC_' then Result := 'NPC'
  else Result := '';
end;

function GlobalTypeFromRecord(e: IInterface): string;
var
  t: string;
begin
  t := LowerCase(GetText(e, 'FNAM - Type'));
  if Pos('short', t) > 0 then
    Result := 'short'
  else if Pos('long', t) > 0 then
    Result := 'long'
  else
    Result := 'float';
end;

function ArtTypeFromRecord(e: IInterface): string;
var
  t: string;
begin
  t := LowerCase(GetText(e, 'DNAM - Art Type'));
  if Pos('hit', t) > 0 then
    Result := 'MagicHitEffect'
  else if Pos('enchant', t) > 0 then
    Result := 'MagicEnchantEffect'
  else
    Result := 'MagicCasting';
end;

function HeadPartTypeFromRecord(e: IInterface): string;
var
  t: string;
begin
  t := LowerCase(GetText(e, 'PNAM - Type'));
  if Pos('face', t) > 0 then Result := 'Face'
  else if Pos('eye', t) > 0 then Result := 'Eyes'
  else if Pos('hair', t) > 0 then Result := 'Hair'
  else if Pos('facial', t) > 0 then Result := 'FacialHair'
  else if Pos('scar', t) > 0 then Result := 'Scar'
  else if Pos('eyebrow', t) > 0 then Result := 'Eyebrows'
  else Result := 'Misc';
end;

procedure AddFormRefArrayFromElement(sl: TStringList; key: string; arr: IInterface);
var
  i: Integer;
  linked: IInterface;
begin
  sl.Add('  ' + JStr(key) + ': [');
  if Assigned(arr) then begin
    for i := 0 to ElementCount(arr) - 1 do begin
      linked := FirstLinkedInElement(ElementByIndex(arr, i));
      if Assigned(linked) then
        sl.Add('    ' + FormRefJson(linked) + ',');
    end;
  end;
  RemoveTrailingComma(sl);
  sl.Add('  ],');
end;

procedure AddRankedFormRefArrayFromElement(sl: TStringList; key: string; arr: IInterface);
var
  i: Integer;
  row, cell, linked: IInterface;
  rank: string;
begin
  sl.Add('  ' + JStr(key) + ': [');
  if Assigned(arr) then begin
    for i := 0 to ElementCount(arr) - 1 do begin
      row := ElementByIndex(arr, i);
      linked := FirstLinkedInElement(row);
      if (not Assigned(linked)) and Assigned(row) and (ElementCount(row) > 0) then begin
        cell := ElementByIndex(row, 0);
        linked := LinksTo(cell);
      end;
      rank := GetFirstText(row, 'Rank', 'FNAM - Faction Rank', 'Perk Rank', 'PRKR - Perk Rank', '0');
      if rank = '' then
        rank := '0';
      if Assigned(linked) then
        sl.Add('    { "form": ' + FormRefJson(linked) + ', "rank": ' + SafeInt(rank, '0') + ' },');
    end;
  end;
  RemoveTrailingComma(sl);
  sl.Add('  ],');
end;

procedure AddFormRefArrayFromPaths(sl: TStringList; key: string; e: IInterface; path1, path2, path3, path4: string);
begin
  AddFormRefArrayFromElement(sl, key, FirstElementByPath(e, path1, path2, path3, path4));
end;

procedure AddRankedFormRefArrayFromPaths(sl: TStringList; key: string; e: IInterface; path1, path2, path3, path4: string);
begin
  AddRankedFormRefArrayFromElement(sl, key, FirstElementByPath(e, path1, path2, path3, path4));
end;

function ActorAggressionValue(const s: string): string;
var
  t: string;
begin
  t := LowerCase(s);
  if Pos('frenzied', t) > 0 then Result := '3'
  else if Pos('very', t) > 0 then Result := '2'
  else if Pos('aggressive', t) > 0 then Result := '1'
  else Result := '0';
end;

function ActorConfidenceValue(const s: string): string;
var
  t: string;
begin
  t := LowerCase(s);
  if Pos('foolhardy', t) > 0 then Result := '4'
  else if Pos('brave', t) > 0 then Result := '3'
  else if Pos('average', t) > 0 then Result := '2'
  else if Pos('cautious', t) > 0 then Result := '1'
  else Result := '0';
end;

function ActorMoralityValue(const s: string): string;
var
  t: string;
begin
  t := LowerCase(s);
  if Pos('no crime', t) > 0 then Result := '3'
  else if Pos('property', t) > 0 then Result := '2'
  else if Pos('violence', t) > 0 then Result := '1'
  else Result := '0';
end;

function ActorMoodValue(const s: string): string;
var
  t: string;
begin
  t := LowerCase(s);
  if Pos('disgust', t) > 0 then Result := '7'
  else if Pos('puzzled', t) > 0 then Result := '6'
  else if Pos('surprised', t) > 0 then Result := '5'
  else if Pos('sad', t) > 0 then Result := '4'
  else if Pos('happy', t) > 0 then Result := '3'
  else if Pos('fear', t) > 0 then Result := '2'
  else if Pos('angry', t) > 0 then Result := '1'
  else Result := '0';
end;

function ActorAssistanceValue(const s: string): string;
var
  t: string;
begin
  t := LowerCase(s);
  if Pos('friends', t) > 0 then Result := '2'
  else if Pos('allies', t) > 0 then Result := '1'
  else Result := '0';
end;

function YesNoBoolJson(const s: string): string;
var
  t: string;
begin
  t := LowerCase(s);
  Result := BoolJson((Pos('true', t) > 0) or (Pos('yes', t) > 0) or (Pos('1', t) > 0));
end;

function PerkEntryFunctionValue(const s: string): string;
var
  t: string;
begin
  t := LowerCase(s);
  if t = '' then Result := '1'
  else if Pos('set value', t) > 0 then Result := '1'
  else if Pos('add value', t) > 0 then Result := '2'
  else if Pos('multiply value', t) > 0 then Result := '3'
  else if Pos('add range', t) > 0 then Result := '4'
  else if Pos('add actor value', t) > 0 then Result := '5'
  else if Pos('absolute value', t) > 0 then Result := '6'
  else if Pos('negative absolute', t) > 0 then Result := '7'
  else if Pos('add leveled list', t) > 0 then Result := '8'
  else if Pos('add activate choice', t) > 0 then Result := '9'
  else if Pos('select spell', t) > 0 then Result := '10'
  else if Pos('select text', t) > 0 then Result := '11'
  else if Pos('set to actor value mult', t) > 0 then Result := '12'
  else if Pos('multiply actor value mult', t) > 0 then Result := '13'
  else if Pos('multiply 1 + actor value mult', t) > 0 then Result := '14'
  else if Pos('set text', t) > 0 then Result := '15'
  else Result := JsonInt(s, '1');
end;

function FirstFloatInElement(e: IInterface; fallback: string): string;
var
  i: Integer;
  text, value: string;
begin
  Result := fallback;
  if not Assigned(e) then
    Exit;

  value := JsonFloat(GetEditValue(e), '');
  if value <> '' then begin
    Result := value;
    Exit;
  end;

  for i := 0 to ElementCount(e) - 1 do begin
    text := FirstFloatInElement(ElementByIndex(e, i), '');
    if text <> '' then begin
      Result := text;
      Exit;
    end;
  end;
end;

procedure AddOutfitFields(sl: TStringList; e: IInterface);
begin
  AddFormRefArrayFromElement(sl, 'outfitPieces', ElementByPath(e, 'INAM - Items'));
end;

function ArmorTypeFromRecord(e: IInterface): string;
var
  t: string;
begin
  t := LowerCase(GetText(e, 'BOD2 - Biped Body Template\Armor Type'));
  if t = '' then
    t := LowerCase(GetText(e, 'BODT - Biped Body Template\Armor Type'));
  if Pos('light', t) > 0 then
    Result := '0'
  else if Pos('heavy', t) > 0 then
    Result := '1'
  else
    Result := '2';
end;

function BipedSlotsFromRecord(e: IInterface): string;
var
  flags: string;
  value: Int64;
begin
  flags := GetText(e, 'BOD2 - Biped Body Template\First Person Flags');
  if flags = '' then
    flags := GetText(e, 'BODT - Biped Body Template\First Person Flags');

  value := 0;
  if HasWord(flags, 'Head') then value := value + 1;
  if HasWord(flags, 'Hair') and (not HasWord(flags, 'Long Hair')) then value := value + 2;
  if HasWord(flags, 'Body') then value := value + 4;
  if HasWord(flags, 'Hands') then value := value + 8;
  if HasWord(flags, 'Forearms') then value := value + 16;
  if HasWord(flags, 'Amulet') then value := value + 32;
  if HasWord(flags, 'Ring') then value := value + 64;
  if HasWord(flags, 'Feet') then value := value + 128;
  if HasWord(flags, 'Calves') then value := value + 256;
  if HasWord(flags, 'Shield') then value := value + 512;
  if HasWord(flags, 'Tail') then value := value + 1024;
  if HasWord(flags, 'Long Hair') then value := value + 2048;
  if HasWord(flags, 'Circlet') then value := value + 4096;
  if HasWord(flags, 'Ears') then value := value + 8192;
  if HasWord(flags, 'Mouth') then value := value + 16384;
  if HasWord(flags, 'Neck') then value := value + 32768;
  if HasWord(flags, 'Chest Primary') then value := value + 65536;
  if HasWord(flags, 'Back') then value := value + 131072;
  if HasWord(flags, 'Misc 1') then value := value + 262144;
  if HasWord(flags, 'Pelvis Primary') then value := value + 524288;
  if HasWord(flags, 'Decapitate Head') then value := value + 1048576;
  if HasWord(flags, 'Decapitate') and (not HasWord(flags, 'Decapitate Head')) then value := value + 2097152;
  if HasWord(flags, 'Pelvis Secondary') then value := value + 4194304;
  if HasWord(flags, 'Leg Right') then value := value + 8388608;
  if HasWord(flags, 'Leg Left') then value := value + 16777216;
  if HasWord(flags, 'Face Jewelry') then value := value + 33554432;
  if HasWord(flags, 'Chest Secondary') then value := value + 67108864;
  if HasWord(flags, 'Shoulder') then value := value + 134217728;
  if HasWord(flags, 'Arm Left') then value := value + 268435456;
  if HasWord(flags, 'Arm Right') then value := value + 536870912;
  if HasWord(flags, 'Misc 2') then value := value + 1073741824;
  if HasWord(flags, 'FX01') then value := value + 2147483648;

  Result := IntToStr(value);
end;

procedure AddBipedFields(sl: TStringList; e: IInterface);
begin
  AddKV(sl, '  ', 'bipedSlots', BipedSlotsFromRecord(e));
  AddKV(sl, '  ', 'armorType', ArmorTypeFromRecord(e));
end;

procedure AddArmorModelFields(sl: TStringList; e: IInterface; includeIcons: Boolean);
begin
  AddStringKV(sl, '  ', 'maleWorldModel', FirstText(e, 'Male world model\MOD2 - Model FileName', 'MOD2 - Model FileName', 'Model\MODL - Model FileName'));
  AddStringKV(sl, '  ', 'femaleWorldModel', FirstText(e, 'Female world model\MOD3 - Model FileName', 'MOD3 - Model FileName', ''));
  AddStringKV(sl, '  ', 'maleFirstPersonModel', FirstText(e, 'Male 1st Person\MOD4 - Model FileName', 'MOD4 - Model FileName', ''));
  AddStringKV(sl, '  ', 'femaleFirstPersonModel', FirstText(e, 'Female 1st Person\MOD5 - Model FileName', 'MOD5 - Model FileName', ''));
  if includeIcons then begin
    AddStringKV(sl, '  ', 'maleInventoryIcon', FirstText(e, 'ICON - Male inventory image', 'ICON - Inventory Image', ''));
    AddStringKV(sl, '  ', 'femaleInventoryIcon', FirstText(e, 'ICO2 - Female inventory image', 'ICO2 - Female Inventory Image', ''));
    AddStringKV(sl, '  ', 'maleMessageIcon', FirstText(e, 'MICO - Male message icon', 'MICO - Message Icon', ''));
    AddStringKV(sl, '  ', 'femaleMessageIcon', FirstText(e, 'MIC2 - Female message icon', 'MIC2 - Female Message Icon', ''));
  end;
end;

procedure AddArmorTypeFields(sl: TStringList; e: IInterface);
begin
  AddBipedFields(sl, e);
  AddArmorModelFields(sl, e, False);
  AddFormRefKV(sl, 'race', LinkedByPath(e, 'RNAM - Race'));
  AddFormRefKV(sl, 'maleSkinTexture', LinkedByPath(e, 'NAM0 - Male Skin Texture'));
  AddFormRefKV(sl, 'femaleSkinTexture', LinkedByPath(e, 'NAM1 - Female Skin Texture'));
  AddFormRefKV(sl, 'maleSkinTextureSwapList', LinkedByPath(e, 'NAM2 - Male Skin Texture Swap List'));
  AddFormRefKV(sl, 'femaleSkinTextureSwapList', LinkedByPath(e, 'NAM3 - Female Skin Texture Swap List'));
  AddFormRefKV(sl, 'footstepSet', LinkedByPath(e, 'SNDD - Footstep Sound'));
  AddFormRefKV(sl, 'armorArtObject', LinkedByPath(e, 'ONAM - Art Object'));
  AddFormRefArrayFromElement(sl, 'additionalRaces', ElementByPath(e, 'Additional Races'));
end;

procedure AddArmorFields(sl: TStringList; e: IInterface);
begin
  AddStringKV(sl, '  ', 'fullName', GetText(e, 'FULL - Name'));
  AddBipedFields(sl, e);
  AddArmorModelFields(sl, e, True);
  AddKV(sl, '  ', 'armorValue', SafeInt(GetText(e, 'DATA - DATA\Value'), '0'));
  AddKV(sl, '  ', 'armorWeight', SafeFloat(GetText(e, 'DATA - DATA\Weight'), '0.0'));
  AddKV(sl, '  ', 'armorRating', SafeFloat(GetText(e, 'DNAM - Armor Rating'), '0.0'));
  AddKV(sl, '  ', 'enchantmentAmount', SafeInt(GetText(e, 'EAMT - Enchantment Amount'), '0'));
  AddFormRefKV(sl, 'race', LinkedByPath(e, 'RNAM - Race'));
  AddFormRefKV(sl, 'enchantment', LinkedByPath(e, 'EITM - Object Effect'));
  AddFormRefKV(sl, 'equipSlot', LinkedByPath(e, 'ETYP - Equipment Type'));
  AddFormRefKV(sl, 'templateArmor', LinkedByPath(e, 'TNAM - Template Armor'));
  AddFormRefKV(sl, 'pickupSound', LinkedByPath(e, 'YNAM - Pick Up Sound'));
  AddFormRefKV(sl, 'putdownSound', LinkedByPath(e, 'ZNAM - Put Down Sound'));
  AddFormRefKV(sl, 'blockBashImpactDataSet', LinkedByPath(e, 'BIDS - Block Bash Impact Data Set'));
  AddFormRefKV(sl, 'altBlockMaterialType', LinkedByPath(e, 'BAMT - Alternate Block Material'));
  AddFormRefArrayFromElement(sl, 'armorAddons', ElementByPath(e, 'Armature'));
  AddFormRefArrayFromElement(sl, 'keywords', ElementByPath(e, 'KWDA - Keywords'));
end;

procedure AddHeadPartFields(sl: TStringList; e: IInterface);
var
  flags: string;
begin
  flags := GetText(e, 'DATA - Flags');
  AddStringKV(sl, '  ', 'fullName', GetText(e, 'FULL - Name'));
  AddStringKV(sl, '  ', 'modelPath', GetText(e, 'Model\MODL - Model FileName'));
  AddStringKV(sl, '  ', 'headPartType', HeadPartTypeFromRecord(e));
  AddKV(sl, '  ', 'playable', BoolJson(HasWord(flags, 'Playable')));
  AddKV(sl, '  ', 'male', BoolJson(HasWord(flags, 'Male')));
  AddKV(sl, '  ', 'female', BoolJson(HasWord(flags, 'Female')));
  AddKV(sl, '  ', 'isExtraPart', BoolJson(HasWord(flags, 'Extra')));
  AddKV(sl, '  ', 'useSolidTint', BoolJson(HasWord(flags, 'Solid')));
  AddStringKV(sl, '  ', 'raceMorphPath', GetText(e, 'Parts\Part #0\NAM1 - FileName'));
  AddStringKV(sl, '  ', 'defaultMorphPath', GetText(e, 'Parts\Part #0\NAM1 - FileName'));
  AddStringKV(sl, '  ', 'chargenMorphPath', GetText(e, 'Parts\Part #1\NAM1 - FileName'));
  AddFormRefKV(sl, 'textureSet', LinkedByPath(e, 'TNAM - Texture Set'));
  AddFormRefKV(sl, 'colorForm', LinkedByPath(e, 'CNAM - Color'));
  AddFormRefKV(sl, 'validRaces', LinkedByPath(e, 'RNAM - Valid Races'));
  AddFormRefArrayFromElement(sl, 'extraParts', ElementByPath(e, 'Extra Parts'));
end;

procedure AddColorFields(sl: TStringList; e: IInterface);
var
  playable: string;
begin
  playable := GetText(e, 'FNAM - Playable');
  AddStringKV(sl, '  ', 'fullName', GetText(e, 'FULL - Name'));
  AddKV(sl, '  ', 'red', ColorChannel(e, 'CNAM - Color\Red', '255'));
  AddKV(sl, '  ', 'green', ColorChannel(e, 'CNAM - Color\Green', '255'));
  AddKV(sl, '  ', 'blue', ColorChannel(e, 'CNAM - Color\Blue', '255'));
  AddKV(sl, '  ', 'alpha', ColorChannel(e, 'CNAM - Color\Alpha', '0'));
  AddKV(sl, '  ', 'playable', BoolJson(HasWord(playable, 'true') or HasWord(playable, 'yes') or HasWord(playable, 'playable')));
end;

procedure AddArtObjectFields(sl: TStringList; e: IInterface);
begin
  AddStringKV(sl, '  ', 'modelPath', GetText(e, 'Model\MODL - Model FileName'));
  AddStringKV(sl, '  ', 'artType', ArtTypeFromRecord(e));
  AddKV(sl, '  ', 'x1', SafeInt(GetText(e, 'OBND - Object Bounds\X1'), '0'));
  AddKV(sl, '  ', 'y1', SafeInt(GetText(e, 'OBND - Object Bounds\Y1'), '0'));
  AddKV(sl, '  ', 'z1', SafeInt(GetText(e, 'OBND - Object Bounds\Z1'), '0'));
  AddKV(sl, '  ', 'x2', SafeInt(GetText(e, 'OBND - Object Bounds\X2'), '0'));
  AddKV(sl, '  ', 'y2', SafeInt(GetText(e, 'OBND - Object Bounds\Y2'), '0'));
  AddKV(sl, '  ', 'z2', SafeInt(GetText(e, 'OBND - Object Bounds\Z2'), '0'));
end;


function FindDescendantByExactName(e: IInterface; const wantedName: string): IInterface;
var
  i: Integer;
  child, found: IInterface;
begin
  Result := nil;
  if not Assigned(e) then
    Exit;

  if LowerCase(Name(e)) = LowerCase(wantedName) then begin
    Result := e;
    Exit;
  end;

  for i := 0 to ElementCount(e) - 1 do begin
    child := ElementByIndex(e, i);
    found := FindDescendantByExactName(child, wantedName);
    if Assigned(found) then begin
      Result := found;
      Exit;
    end;
  end;
end;

function FindDescendantByNameContains(e: IInterface; const wantedText: string): IInterface;
var
  i: Integer;
  child, found: IInterface;
begin
  Result := nil;
  if not Assigned(e) then
    Exit;

  if Pos(LowerCase(wantedText), LowerCase(Name(e))) > 0 then begin
    Result := e;
    Exit;
  end;

  for i := 0 to ElementCount(e) - 1 do begin
    child := ElementByIndex(e, i);
    found := FindDescendantByNameContains(child, wantedText);
    if Assigned(found) then begin
      Result := found;
      Exit;
    end;
  end;
end;

function NativeIntOfElement(e: IInterface; fallback: Integer): Integer;
begin
  Result := fallback;
  if not Assigned(e) then
    Exit;

  try
    Result := GetNativeValue(e);
  except
    Result := fallback;
  end;
end;

function ElementIntegerText(e: IInterface; const fallback: string): string;
var
  nativeValue: Integer;
  editValue: string;
begin
  Result := fallback;
  if not Assigned(e) then
    Exit;

  nativeValue := NativeIntOfElement(e, -2147483647);
  if nativeValue <> -2147483647 then begin
    Result := IntToStr(nativeValue);
    Exit;
  end;

  editValue := GetEditValue(e);
  Result := JsonInt(editValue, fallback);
end;

function IsActualConditionNode(e: IInterface): Boolean;
var
  nodeName, nodeSignature: string;
begin
  Result := False;
  if not Assigned(e) then
    Exit;

  nodeName := LowerCase(Name(e));
  nodeSignature := UpperCase(Signature(e));

  Result :=
    (nodeSignature = 'CTDA') or
    (Pos('condition #', nodeName) > 0) or
    (nodeName = 'condition') or
    (Pos('ctda -', nodeName) = 1);
end;

function EnabledNamedFlagRecursive(e: IInterface; const flagName: string): Boolean;
var
  i: Integer;
  child: IInterface;
begin
  Result := False;
  if not Assigned(e) then
    Exit;

  if LowerCase(Name(e)) = LowerCase(flagName) then begin
    Result := FlagTextIsEnabled(GetEditValue(e));
    Exit;
  end;

  for i := 0 to ElementCount(e) - 1 do begin
    child := ElementByIndex(e, i);
    if EnabledNamedFlagRecursive(child, flagName) then begin
      Result := True;
      Exit;
    end;
  end;
end;

function PerkConditionKindFromFunctionText(const functionText: string): string;
var
  normalized: string;
begin
  normalized := LowerCase(functionText);
  normalized := StringReplace(normalized, ' ', '', [rfReplaceAll]);
  normalized := StringReplace(normalized, '-', '', [rfReplaceAll]);

  if Pos('getglobalvalue', normalized) > 0 then
    Result := 'GetGlobalValue'
  else if Pos('getbaseactorvalue', normalized) > 0 then
    Result := 'GetBaseActorValue'
  else if Pos('getactorvalue', normalized) > 0 then
    Result := 'GetActorValue'
  else if Pos('hasperk', normalized) > 0 then
    Result := 'HasPerk'
  else if Pos('getquestcompleted', normalized) > 0 then
    Result := 'GetQuestCompleted'
  else if Pos('hasspell', normalized) > 0 then
    Result := 'HasSpell'
  else
    Result := 'Raw';
end;

function ConditionOpCode(conditionNode, typeElement: IInterface): Integer;
var
  nativeType: Integer;
  summary: string;
begin
  Result := 0;

  nativeType := NativeIntOfElement(typeElement, -1);
  if nativeType >= 0 then begin
    Result := nativeType mod 8;
    if Result <= 5 then
      Exit;
  end;

  summary := UpperCase(GetEditValue(conditionNode));

  if (Pos('!=', summary) > 0) or (Pos('<>', summary) > 0) then
    Result := 1
  else if Pos('>=', summary) > 0 then
    Result := 3
  else if Pos('<=', summary) > 0 then
    Result := 5
  else if Pos('>', summary) > 0 then
    Result := 2
  else if Pos('<', summary) > 0 then
    Result := 4
  else
    Result := 0;
end;

function ConditionIsOr(conditionNode, typeElement: IInterface): Boolean;
var
  summary: string;
begin
  summary := ' ' + UpperCase(Trim(GetEditValue(conditionNode))) + ' ';

  Result :=
    (Pos(' OR ', summary) > 0) or
    EnabledNamedFlagRecursive(typeElement, 'OR');
end;

function ConditionParameterText(parameterElement: IInterface): string;
var
  linked: IInterface;
  nativeValue: Integer;
  editValue: string;
begin
  Result := '';
  if not Assigned(parameterElement) then
    Exit;

  linked := FirstLinkedInElement(parameterElement);
  if Assigned(linked) then begin
    Result := PluginLocalID(linked);
    Exit;
  end;

  editValue := Trim(GetEditValue(parameterElement));
  if editValue = '' then
    Exit;

  nativeValue := NativeIntOfElement(parameterElement, -2147483647);
  if nativeValue <> -2147483647 then begin
    Result := IntToStr(nativeValue);
    Exit;
  end;

  if LowerCase(editValue) <> 'none' then
    Result := editValue;
end;

procedure WritePerkConditionJson(
  sl: TStringList;
  conditionNode: IInterface;
  const indent: string;
  var writtenCount: Integer
);
var
  functionElement, typeElement, comparisonElement: IInterface;
  param1Element, param2Element, comparisonLinked: IInterface;
  functionText, kind, comparisonValue: string;
  comparisonGlobal, param1, param2, conditionText: string;
  functionId, opCode: Integer;
  isOr, useGlobalComparison: Boolean;
begin
  functionElement := FindDescendantByExactName(conditionNode, 'Function');
  if not Assigned(functionElement) then begin
    AddMessage('[DFG][PERK] Ignored condition without Function: ' + Name(conditionNode));
    Exit;
  end;

  typeElement := FindDescendantByExactName(conditionNode, 'Type');
  comparisonElement := FindDescendantByNameContains(conditionNode, 'Comparison Value');
  param1Element := FindDescendantByExactName(conditionNode, 'Parameter #1');
  param2Element := FindDescendantByExactName(conditionNode, 'Parameter #2');

  functionText := GetEditValue(functionElement);
  kind := PerkConditionKindFromFunctionText(functionText);
  functionId := NativeIntOfElement(functionElement, 0);
  opCode := ConditionOpCode(conditionNode, typeElement);
  isOr := ConditionIsOr(conditionNode, typeElement);

  comparisonLinked := nil;
  if Assigned(comparisonElement) then
    comparisonLinked := FirstLinkedInElement(comparisonElement);

  useGlobalComparison :=
    Assigned(comparisonLinked) or
    EnabledNamedFlagRecursive(typeElement, 'Use Global');

  comparisonGlobal := '';
  comparisonValue := '0.0';

  if useGlobalComparison and Assigned(comparisonLinked) then
    comparisonGlobal := PluginLocalID(comparisonLinked)
  else if Assigned(comparisonElement) then
    comparisonValue := JsonFloat(GetEditValue(comparisonElement), '0.0');

  param1 := ConditionParameterText(param1Element);
  param2 := ConditionParameterText(param2Element);
  conditionText := GetEditValue(conditionNode);

  sl.Add(indent + '{');
  AddStringKV(sl, indent + '  ', 'kind', kind);
  AddKV(sl, indent + '  ', 'functionId', IntToStr(functionId));
  AddKV(sl, indent + '  ', 'opCode', IntToStr(opCode));
  AddKV(sl, indent + '  ', 'comparisonValue', comparisonValue);
  AddKV(sl, indent + '  ', 'isOr', BoolJson(isOr));
  AddKV(sl, indent + '  ', 'useGlobalComparison', BoolJson(useGlobalComparison));
  AddStringKV(sl, indent + '  ', 'comparisonGlobal', comparisonGlobal);
  AddStringKV(sl, indent + '  ', 'param1', param1);
  AddStringKV(sl, indent + '  ', 'param2', param2);
  RemoveTrailingComma(sl);
  sl.Add(indent + '},');

  Inc(writtenCount);

  AddMessage(
    '[DFG][PERK] Condition #' + IntToStr(writtenCount - 1) +
    ' kind=' + kind +
    ' functionId=' + IntToStr(functionId) +
    ' opCode=' + IntToStr(opCode) +
    ' comparison=' + comparisonValue +
    ' OR=' + DebugBoolText(isOr) +
    ' global=' + DebugBoolText(useGlobalComparison) +
    ' param1="' + param1 + '"' +
    ' param2="' + param2 + '"' +
    ' text="' + conditionText + '"'
  );
end;

procedure WritePerkConditionsRecursive(
  sl: TStringList;
  node: IInterface;
  const indent: string;
  var writtenCount: Integer
);
var
  i: Integer;
  child: IInterface;
begin
  if not Assigned(node) then
    Exit;

  if IsActualConditionNode(node) then begin
    WritePerkConditionJson(sl, node, indent, writtenCount);
    Exit;
  end;

  for i := 0 to ElementCount(node) - 1 do begin
    child := ElementByIndex(node, i);
    WritePerkConditionsRecursive(sl, child, indent, writtenCount);
  end;
end;

procedure AddPerkConditionsArray(
  sl: TStringList;
  key: string;
  arr: IInterface;
  indent: string
);
var
  writtenCount: Integer;
begin
  writtenCount := 0;

  sl.Add(indent + JStr(key) + ': [');
  if Assigned(arr) then
    WritePerkConditionsRecursive(sl, arr, indent + '  ', writtenCount);

  RemoveTrailingComma(sl);
  sl.Add(indent + '],');

  AddMessage(
    '[DFG][PERK] Exported ' + IntToStr(writtenCount) +
    ' conditions for array "' + key + '"'
  );
end;

procedure AddPerkEntriesArray(sl: TStringList; e: IInterface);
var
  arr, row, header: IInterface;
  entryPointElement, functionElement, numArgsElement, valueElement: IInterface;
  i, entryCount: Integer;
  rank, priority, entryPoint, functionValue, numArgs, value: string;
begin
  arr := FirstElementByPath(e, 'Effects', 'PRKE - Effects', 'Perk Entries', 'Entries');
  if not Assigned(arr) then
    arr := FindChildByNameWord(e, 'Effect');

  entryCount := 0;
  sl.Add('  "entries": [');

  if Assigned(arr) then begin
    for i := 0 to ElementCount(arr) - 1 do begin
      row := ElementByIndex(arr, i);

      // Skip helper/marker nodes that are not actual perk effects.
      if not Assigned(FindDescendantByNameContains(row, 'PRKE - Header')) then
        if not Assigned(FindDescendantByExactName(row, 'Rank')) then
          Continue;

      header := FirstElementByPath(row, 'PRKE - Header', 'Header', 'PRKE', '');
      if not Assigned(header) then
        header := row;

      rank := JsonInt(
        GetFirstText(
          header,
          'Rank',
          'PRKE - Header\Rank',
          'Perk Rank',
          'PRKR - Perk Rank',
          '0'
        ),
        '0'
      );

      priority := JsonInt(
        GetFirstText(
          header,
          'Priority',
          'PRKE - Header\Priority',
          '',
          '',
          '0'
        ),
        '0'
      );

      entryPointElement := FirstElementByPath(
        row,
        'DATA - Effect Data\Entry Point\Entry Point',
        'DATA\Entry Point\Entry Point',
        'Effect Data\Entry Point\Entry Point',
        'Entry Point\Entry Point'
      );
      if not Assigned(entryPointElement) then
        entryPointElement := FindDescendantByExactName(row, 'Entry Point');

      entryPoint := ElementIntegerText(entryPointElement, '75');

      functionElement := FirstElementByPath(
        row,
        'DATA - Effect Data\Entry Point\Function',
        'DATA\Entry Point\Function',
        'Effect Data\Entry Point\Function',
        'Entry Point\Function'
      );
      if not Assigned(functionElement) then
        functionElement := FindDescendantByExactName(row, 'Function');

      functionValue := PerkEntryFunctionValue(GetEditValue(functionElement));

      numArgsElement := FirstElementByPath(
        row,
        'DATA - Effect Data\Entry Point\Perk Condition Tab Count',
        'DATA\Entry Point\Perk Condition Tab Count',
        'Effect Data\Entry Point\Perk Condition Tab Count',
        'Entry Point\Perk Condition Tab Count'
      );
      if not Assigned(numArgsElement) then
        numArgsElement := FindDescendantByNameContains(
          row,
          'Perk Condition Tab Count'
        );

      numArgs := ElementIntegerText(numArgsElement, '0');

      valueElement := FirstElementByPath(
        row,
        'Function Parameters\EPFD - Data\Float',
        'Function Parameters\EPFD - Data',
        'EPFD - Data\Float',
        'EPFD - Data'
      );
      if not Assigned(valueElement) then
        valueElement := FindDescendantByNameContains(row, 'EPFD - Data');

      value := FirstFloatInElement(valueElement, '1.0');

      sl.Add('    {');
      AddKV(sl, '      ', 'rank', rank);
      AddKV(sl, '      ', 'priority', priority);
      AddKV(sl, '      ', 'entryPoint', entryPoint);
      AddKV(sl, '      ', 'function', functionValue);
      AddKV(sl, '      ', 'numArgs', numArgs);
      AddKV(sl, '      ', 'value', value);

      // Perk-condition tabs are flattened in their original xEdit order because
      // the current DFG JSON schema stores a single condition array per entry.
      AddPerkConditionsArray(sl, 'conditions', row, '      ');

      RemoveTrailingComma(sl);
      sl.Add('    },');

      AddMessage(
        '[DFG][PERK] Entry #' + IntToStr(entryCount) +
        ' rank=' + rank +
        ' priority=' + priority +
        ' entryPoint=' + entryPoint +
        ' function=' + functionValue +
        ' numArgs=' + numArgs +
        ' value=' + value
      );

      Inc(entryCount);
    end;
  end;

  RemoveTrailingComma(sl);
  sl.Add('  ],');

  AddMessage('[DFG][PERK] Exported entries: ' + IntToStr(entryCount));
end;

procedure AddPerkFields(sl: TStringList; e: IInterface);
var
  data: string;
  topConditions: IInterface;
begin
  AddMessage('[DFG][PERK] ================================================');
  AddMessage('[DFG][PERK] Exporting ' + Name(e));

  data := GetText(e, 'DATA - Data');
  AddStringKV(sl, '  ', 'fullName', GetText(e, 'FULL - Name'));
  AddStringKV(sl, '  ', 'description', GetText(e, 'DESC - Description'));
  AddKV(sl, '  ', 'trait', BoolJson(HasWord(data, 'Trait')));
  AddKV(sl, '  ', 'level', SafeInt(GetText(e, 'DATA - Data\Level'), '0'));
  AddKV(sl, '  ', 'numRanks', SafeInt(GetText(e, 'DATA - Data\Num Ranks'), '1'));
  AddKV(sl, '  ', 'playable', BoolJson(not HasWord(data, 'Playable: False')));
  AddKV(sl, '  ', 'hidden', BoolJson(HasWord(data, 'Hidden: True')));
  AddFormRefKV(sl, 'nextPerk', LinkedByPath(e, 'NNAM - Next Perk'));

  topConditions := ElementByPath(e, 'Conditions');
  AddPerkConditionsArray(sl, 'conditions', topConditions, '  ');
  AddPerkEntriesArray(sl, e);

  AddMessage('[DFG][PERK] ================================================');
end;


procedure AddLightFields(sl: TStringList; e: IInterface);
begin
  AddStringKV(sl, '  ', 'fullName', GetText(e, 'FULL - Name'));
  AddStringKV(sl, '  ', 'modelPath', GetText(e, 'Model\MODL - Model FileName'));
  AddKV(sl, '  ', 'lightTime', SafeInt(GetText(e, 'DATA - DATA\Time'), '-1'));
  AddKV(sl, '  ', 'lightRadius', SafeInt(GetText(e, 'DATA - DATA\Radius'), '0'));
  AddKV(sl, '  ', 'red', ColorChannel(e, 'DATA - DATA\Color\Red', '255'));
  AddKV(sl, '  ', 'green', ColorChannel(e, 'DATA - DATA\Color\Green', '255'));
  AddKV(sl, '  ', 'blue', ColorChannel(e, 'DATA - DATA\Color\Blue', '255'));
  AddKV(sl, '  ', 'alpha', '0');
  AddKV(sl, '  ', 'flags', '0');
  AddKV(sl, '  ', 'falloffExponent', SafeFloat(GetText(e, 'DATA - DATA\Falloff Exponent'), '1.0'));
  AddKV(sl, '  ', 'fov', SafeFloat(GetText(e, 'DATA - DATA\FOV'), '90.0'));
  AddKV(sl, '  ', 'nearClip', SafeFloat(GetText(e, 'DATA - DATA\Near Clip'), '0.0'));
  AddKV(sl, '  ', 'fade', SafeFloat(GetText(e, 'FNAM - Fade value'), '1.0'));
  AddFormRefKV(sl, 'sound', LinkedByPath(e, 'SNAM - Sound'));
  AddFormRefKV(sl, 'lensFlare', LinkedByPath(e, 'LNAM - Lens'));
end;

procedure AddExplosionFields(sl: TStringList; e: IInterface);
begin
  AddStringKV(sl, '  ', 'fullName', GetText(e, 'FULL - Name'));
  AddStringKV(sl, '  ', 'modelPath', GetText(e, 'Model\MODL - Model FileName'));
  AddFormRefKV(sl, 'light', LinkedByPath(e, 'DATA - Data\Light'));
  AddFormRefKV(sl, 'sound1', LinkedByPath(e, 'DATA - Data\Sound 1'));
  AddFormRefKV(sl, 'sound2', LinkedByPath(e, 'DATA - Data\Sound 2'));
  AddFormRefKV(sl, 'impactDataSet', LinkedByPath(e, 'DATA - Data\Impact Data Set'));
  AddFormRefKV(sl, 'spawnProjectile', LinkedByPath(e, 'DATA - Data\Spawn Projectile'));
  AddFormRefKV(sl, 'objectEffect', LinkedByPath(e, 'EITM - Object Effect'));
  AddFormRefKV(sl, 'imageSpaceModifier', LinkedByPath(e, 'MNAM - Image Space Modifier'));
  AddKV(sl, '  ', 'force', SafeFloat(GetText(e, 'DATA - Data\Force'), '0.0'));
  AddKV(sl, '  ', 'damage', SafeFloat(GetText(e, 'DATA - Data\Damage'), '0.0'));
  AddKV(sl, '  ', 'radius', SafeFloat(GetText(e, 'DATA - Data\Radius'), '0.0'));
  AddKV(sl, '  ', 'imageSpaceRadius', SafeFloat(GetText(e, 'DATA - Data\IS Radius'), '0.0'));
  AddKV(sl, '  ', 'verticalOffsetMult', SafeFloat(GetText(e, 'DATA - Data\Vertical Offset Mult'), '0.0'));
  AddKV(sl, '  ', 'flags', '0');
  AddKV(sl, '  ', 'soundLevel', '0');
end;

procedure AddActivatorFields(sl: TStringList; e: IInterface);
begin
  AddStringKV(sl, '  ', 'fullName', GetText(e, 'FULL - Name'));
  AddStringKV(sl, '  ', 'modelPath', GetText(e, 'Model\MODL - Model FileName'));
  AddFormRefKV(sl, 'soundLoop', LinkedByPath(e, 'SNAM - Sound - Looping'));
  AddFormRefKV(sl, 'soundActivate', LinkedByPath(e, 'VNAM - Sound - Activation'));
  AddFormRefKV(sl, 'waterType', LinkedByPath(e, 'WNAM - Water Type'));
  AddKV(sl, '  ', 'flags', '0');
end;

procedure AddEffectShaderColor(sl: TStringList; prefix: string; e: IInterface; path: string);
begin
  AddKV(sl, '  ', prefix + 'Red', ColorChannel(e, path + '\Red', '255'));
  AddKV(sl, '  ', prefix + 'Green', ColorChannel(e, path + '\Green', '255'));
  AddKV(sl, '  ', prefix + 'Blue', ColorChannel(e, path + '\Blue', '255'));
  AddKV(sl, '  ', prefix + 'Alpha', ColorChannel(e, path + '\Alpha', '0'));
end;

procedure AddEffectShaderFields(sl: TStringList; e: IInterface);
begin
  AddStringKV(sl, '  ', 'fillTexture', GetText(e, 'ICON - Fill Texture'));
  AddStringKV(sl, '  ', 'particleShaderTexture', GetText(e, 'ICO2 - Particle Shader Texture'));
  AddStringKV(sl, '  ', 'holesTexture', GetText(e, 'NAM7 - Holes Texture'));
  AddStringKV(sl, '  ', 'membranePaletteTexture', GetText(e, 'NAM8 - Membrane Palette Texture'));
  AddStringKV(sl, '  ', 'particlePaletteTexture', GetText(e, 'NAM9 - Particle Palette Texture'));
  AddFormRefKV(sl, 'ambientSound', LinkedByPath(e, 'DATA - Data\Ambient Sound'));
  AddKV(sl, '  ', 'flags', '0');
  AddEffectShaderColor(sl, 'fillColor1', e, 'DATA - Data\Fill Texture Effect Color Key 1');
  AddEffectShaderColor(sl, 'fillColor2', e, 'DATA - Data\Fill Texture Effect Color Key 2');
  AddEffectShaderColor(sl, 'fillColor3', e, 'DATA - Data\Fill Texture Effect Color Key 3');
  AddEffectShaderColor(sl, 'edgeEffect', e, 'DATA - Data\Edge Effect Color');
  AddEffectShaderColor(sl, 'edgeColor', e, 'DATA - Data\Edge Color');
  AddEffectShaderColor(sl, 'particleColor1', e, 'DATA - Data\Color Key 1');
  AddEffectShaderColor(sl, 'particleColor2', e, 'DATA - Data\Color Key 2');
  AddEffectShaderColor(sl, 'particleColor3', e, 'DATA - Data\Color Key 3');
  AddKV(sl, '  ', 'fillAlphaFadeIn', SafeFloat(GetText(e, 'DATA - Data\Fill Texture Effect Alpha Fade In Time'), '0.0'));
  AddKV(sl, '  ', 'fillFullAlphaTime', SafeFloat(GetText(e, 'DATA - Data\Fill Texture Effect Full Alpha Time'), '0.0'));
  AddKV(sl, '  ', 'fillAlphaFadeOut', SafeFloat(GetText(e, 'DATA - Data\Fill Texture Effect Alpha Fade Out Time'), '0.0'));
  AddKV(sl, '  ', 'fillPersistentAlphaRatio', SafeFloat(GetText(e, 'DATA - Data\Fill Texture Effect Persistent Alpha Ratio'), '0.0'));
  AddKV(sl, '  ', 'fillAlphaPulseAmplitude', SafeFloat(GetText(e, 'DATA - Data\Fill Texture Effect Alpha Pulse Amplitude'), '0.0'));
  AddKV(sl, '  ', 'fillAlphaPulseFrequency', SafeFloat(GetText(e, 'DATA - Data\Fill Texture Effect Alpha Pulse Frequency'), '0.0'));
  AddKV(sl, '  ', 'fillTextureAnimationSpeedU', SafeFloat(GetText(e, 'DATA - Data\Fill Texture Effect Texture Animation Speed U'), '0.0'));
  AddKV(sl, '  ', 'fillTextureAnimationSpeedV', SafeFloat(GetText(e, 'DATA - Data\Fill Texture Effect Texture Animation Speed V'), '0.0'));
  AddKV(sl, '  ', 'fillTextureScaleU', SafeFloat(GetText(e, 'DATA - Data\Fill Texture Effect Texture Scale U'), '1.0'));
  AddKV(sl, '  ', 'fillTextureScaleV', SafeFloat(GetText(e, 'DATA - Data\Fill Texture Effect Texture Scale V'), '1.0'));
  AddKV(sl, '  ', 'fillFullAlphaRatio', SafeFloat(GetText(e, 'DATA - Data\Fill Texture Effect Full Alpha Ratio'), '1.0'));
  AddKV(sl, '  ', 'edgeFalloff', SafeFloat(GetText(e, 'DATA - Data\Edge Effect Fall Off'), '0.0'));
  AddKV(sl, '  ', 'edgeAlphaFadeIn', SafeFloat(GetText(e, 'DATA - Data\Edge Effect Alpha Fade In Time'), '0.0'));
  AddKV(sl, '  ', 'edgeFullAlphaTime', SafeFloat(GetText(e, 'DATA - Data\Edge Effect Full Alpha Time'), '0.0'));
  AddKV(sl, '  ', 'edgeAlphaFadeOut', SafeFloat(GetText(e, 'DATA - Data\Edge Effect Alpha Fade Out Time'), '0.0'));
  AddKV(sl, '  ', 'edgePersistentAlphaRatio', SafeFloat(GetText(e, 'DATA - Data\Edge Effect Persistent Alpha Ratio'), '0.0'));
  AddKV(sl, '  ', 'edgeAlphaPulseAmplitude', SafeFloat(GetText(e, 'DATA - Data\Edge Effect Alpha Pulse Amplitude'), '0.0'));
  AddKV(sl, '  ', 'edgeAlphaPulseFrequency', SafeFloat(GetText(e, 'DATA - Data\Edge Effect Alpha Pulse Frequency'), '0.0'));
  AddKV(sl, '  ', 'edgeFullAlphaRatio', SafeFloat(GetText(e, 'DATA - Data\Edge Effect Full Alpha Ratio'), '1.0'));
  AddKV(sl, '  ', 'edgeWidthAlphaUnits', SafeFloat(GetText(e, 'DATA - Data\Edge Width Alpha Units'), '0.0'));
  AddKV(sl, '  ', 'particleBirthRampUpTime', SafeFloat(GetText(e, 'DATA - Data\Particle Shader Particle Birth Ramp Up Time'), '0.0'));
  AddKV(sl, '  ', 'particleFullBirthTime', SafeFloat(GetText(e, 'DATA - Data\Particle Shader Full Particle Birth Time'), '0.0'));
  AddKV(sl, '  ', 'particleBirthRampDownTime', SafeFloat(GetText(e, 'DATA - Data\Particle Shader Particle Birth Ramp Down Time'), '0.0'));
  AddKV(sl, '  ', 'particleFullBirthRatio', SafeFloat(GetText(e, 'DATA - Data\Particle Shader Full Particle Birth Ratio'), '1.0'));
  AddKV(sl, '  ', 'particleCount', SafeFloat(GetText(e, 'DATA - Data\Particle Shader Persistent Particle Count'), '0.0'));
  AddKV(sl, '  ', 'particleLifetime', SafeFloat(GetText(e, 'DATA - Data\Particle Shader Particle Lifetime'), '0.0'));
  AddKV(sl, '  ', 'particleLifetimeVariance', SafeFloat(GetText(e, 'DATA - Data\Particle Shader Particle Lifetime Variance'), '0.0'));
  AddKV(sl, '  ', 'particleInitialSpeedAlongNormal', SafeFloat(GetText(e, 'DATA - Data\Particle Shader Initial Speed Along Normal'), '0.0'));
  AddKV(sl, '  ', 'particleAccelerationAlongNormal', SafeFloat(GetText(e, 'DATA - Data\Particle Shader Acceleration Along Normal'), '0.0'));
  AddKV(sl, '  ', 'particleScaleKey1', SafeFloat(GetText(e, 'DATA - Data\Particle Shader Scale Key 1'), '1.0'));
  AddKV(sl, '  ', 'particleScaleKey2', SafeFloat(GetText(e, 'DATA - Data\Particle Shader Scale Key 2'), '1.0'));
  AddKV(sl, '  ', 'particleScaleKey1Time', SafeFloat(GetText(e, 'DATA - Data\Particle Shader Scale Key 1 Time'), '0.0'));
  AddKV(sl, '  ', 'particleScaleKey2Time', SafeFloat(GetText(e, 'DATA - Data\Particle Shader Scale Key 2 Time'), '1.0'));
  AddKV(sl, '  ', 'particleColor1AlphaValue', SafeFloat(GetText(e, 'DATA - Data\Color Key 1 Alpha'), '1.0'));
  AddKV(sl, '  ', 'particleColor2AlphaValue', SafeFloat(GetText(e, 'DATA - Data\Color Key 2 Alpha'), '1.0'));
  AddKV(sl, '  ', 'particleColor3AlphaValue', SafeFloat(GetText(e, 'DATA - Data\Color Key 3 Alpha'), '1.0'));
  AddKV(sl, '  ', 'particleColor1Time', SafeFloat(GetText(e, 'DATA - Data\Color Key 1 Time'), '0.0'));
  AddKV(sl, '  ', 'particleColor2Time', SafeFloat(GetText(e, 'DATA - Data\Color Key 2 Time'), '0.5'));
  AddKV(sl, '  ', 'particleColor3Time', SafeFloat(GetText(e, 'DATA - Data\Color Key 3 Time'), '1.0'));
end;

procedure AddNPCFields(sl: TStringList; e: IInterface);
var
  flags: string;
begin
  flags := NpcFlagsText(e);

  DebugNpcFlagsRecord(e, flags);

  if ShouldDebugNpc(e) then begin
    AddMessage('[DFG][FLAGS] Booleans read directly from ACBS\\Flags children:');
    AddMessage('[DFG][FLAGS]   female=' +
      DebugBoolText(NpcFlagEnabled(e, 'Female')));
    AddMessage('[DFG][FLAGS]   oppositeGenderAnim=' +
      DebugBoolText(NpcFlagEnabledEither(
        e,
        'Opposite Gender Anims',
        'Opposite Gender Animations'
      )));
    AddMessage('[DFG][FLAGS]   essential=' +
      DebugBoolText(NpcFlagEnabled(e, 'Essential')));
    AddMessage('[DFG][FLAGS]   protected=' +
      DebugBoolText(NpcFlagEnabled(e, 'Protected')));
    AddMessage('[DFG][FLAGS]   unique=' +
      DebugBoolText(NpcFlagEnabled(e, 'Unique')));
    AddMessage('[DFG][FLAGS]   calcStats=' +
      DebugBoolText(
        NpcFlagEnabled(e, 'Auto-calc stats') or
        NpcFlagEnabled(e, 'PC Level Mult')
      ));
    AddMessage('[DFG][FLAGS]   respawn=' +
      DebugBoolText(NpcFlagEnabled(e, 'Respawn')));
    AddMessage('[DFG][FLAGS] ==================================================');
  end;

  if flags = '' then
    AddMessage('[DFG] NPC flags not found: ' + Name(e));
  AddStringKV(sl, '  ', 'fullName', GetText(e, 'FULL - Name'));
  AddFormRefKV(sl, 'race', LinkedByPath(e, 'RNAM - Race'));
  AddFormRefKV(sl, 'skin', LinkedByPath(e, 'WNAM - Skin'));
  AddFormRefKV(sl, 'defaultOutfit', LinkedByPath(e, 'DOFT - Default Outfit'));
  AddFormRefKV(sl, 'sleepOutfit', LinkedByPath(e, 'SOFT - Sleeping Outfit'));
  AddFormRefKV(sl, 'voice', LinkedByPath(e, 'VTCK - Voice'));
  AddFormRefKV(sl, 'hairColor', LinkedByPath(e, 'HCLF - Hair Color'));
  AddFormRefKV(sl, 'faceTexture', LinkedByPath(e, 'FTST - Head texture'));
  AddFormRefKV(sl, 'class', LinkedByPath(e, 'CNAM - Class'));
  AddFormRefKV(sl, 'combatStyle', LinkedByPath(e, 'ZNAM - Combat Style'));
  AddFormRefKV(sl, 'giftFilter', LinkedByPath(e, 'GNAM - Gift Filter'));
  AddFormRefKV(sl, 'deathItem', LinkedByPath(e, 'INAM - Death item'));
  AddFormRefKV(sl, 'defaultPackageList', LinkedByPath(e, 'DPLT - Default Package List'));
  AddFormRefKV(sl, 'crimeFaction', LinkedByPath(e, 'CRIF - Crime Faction'));
  AddKV(sl, '  ', 'female',
    BoolJson(NpcFlagEnabled(e, 'Female')));
  AddKV(sl, '  ', 'oppositeGenderAnim',
    BoolJson(NpcFlagEnabledEither(
      e,
      'Opposite Gender Anims',
      'Opposite Gender Animations'
    )));
  AddKV(sl, '  ', 'essential',
    BoolJson(NpcFlagEnabled(e, 'Essential')));
  AddKV(sl, '  ', 'protected',
    BoolJson(NpcFlagEnabled(e, 'Protected')));
  AddKV(sl, '  ', 'unique',
    BoolJson(NpcFlagEnabled(e, 'Unique')));
  AddKV(sl, '  ', 'calcStats',
    BoolJson(
      NpcFlagEnabled(e, 'Auto-calc stats') or
      NpcFlagEnabled(e, 'PC Level Mult')
    ));
  AddKV(sl, '  ', 'respawn',
    BoolJson(NpcFlagEnabled(e, 'Respawn')));
  AddKV(sl, '  ', 'doesntAffectStealthMeter',
    BoolJson(NpcFlagEnabledEither(
      e,
      'Doesn''t affect stealth meter',
      'Does not affect stealth meter'
    )));
  AddKV(sl, '  ', 'doesntBleed',
    BoolJson(NpcFlagEnabledEither(
      e,
      'Doesn''t Bleed',
      'Does not bleed'
    )));
  AddKV(sl, '  ', 'bleedoutOverrideFlag',
    BoolJson(NpcFlagEnabled(e, 'Bleedout Override')));
  AddKV(sl, '  ', 'simpleActor',
    BoolJson(NpcFlagEnabled(e, 'Simple Actor')));
  AddKV(sl, '  ', 'noActivation',
    BoolJson(NpcFlagEnabled(e, 'No Activation')));
  AddKV(sl, '  ', 'ghost',
    BoolJson(NpcFlagEnabled(e, 'Ghost')));
  AddKV(sl, '  ', 'invulnerable',
    BoolJson(NpcFlagEnabled(e, 'Invulnerable')));
  AddKV(sl, '  ', 'height', SafeFloat(GetText(e, 'NAM6 - Height'), '1.0'));
  AddKV(sl, '  ', 'weight', SafeFloat(GetText(e, 'NAM7 - Weight'), '50.0'));
  AddKV(sl, '  ', 'red', ColorChannel(e, 'QNAM - Texture lighting\Red', '255'));
  AddKV(sl, '  ', 'green', ColorChannel(e, 'QNAM - Texture lighting\Green', '255'));
  AddKV(sl, '  ', 'blue', ColorChannel(e, 'QNAM - Texture lighting\Blue', '255'));
  AddKV(sl, '  ', 'alpha', ColorChannel(e, 'QNAM - Texture lighting\Alpha', '0'));
  AddKV(sl, '  ', 'health', SafeInt(GetText(e, 'DNAM - Player Skills\Health'), '100'));
  AddKV(sl, '  ', 'magicka', SafeInt(GetText(e, 'DNAM - Player Skills\Magicka'), '50'));
  AddKV(sl, '  ', 'stamina', SafeInt(GetText(e, 'DNAM - Player Skills\Stamina'), '50'));
  AddKV(sl, '  ', 'healthOffset', SafeInt(GetText(e, 'ACBS - Configuration\Health Offset'), '0'));
  AddKV(sl, '  ', 'magickaOffset', SafeInt(GetText(e, 'ACBS - Configuration\Magicka Offset'), '0'));
  AddKV(sl, '  ', 'staminaOffset', SafeInt(GetText(e, 'ACBS - Configuration\Stamina Offset'), '0'));
  AddKV(sl, '  ', 'calcMinLevel', SafeInt(GetText(e, 'ACBS - Configuration\Calc min level'), '1'));
  AddKV(sl, '  ', 'calcMaxLevel', SafeInt(GetText(e, 'ACBS - Configuration\Calc max level'), '1'));
  AddKV(sl, '  ', 'npcLevel', SafeInt(GetText(e, 'ACBS - Configuration\Level'), '1'));
  AddKV(sl, '  ', 'speedMult', SafeInt(GetText(e, 'ACBS - Configuration\Speed Mult'), '100'));
  AddKV(sl, '  ', 'dispositionBase', SafeInt(GetText(e, 'ACBS - Configuration\Disposition Base'), '35'));
  AddKV(sl, '  ', 'bleedoutOverride', SafeInt(GetText(e, 'ACBS - Configuration\Bleedout Override'), '0'));
  AddKV(sl, '  ', 'aiAggression', ActorAggressionValue(GetText(e, 'AIDT - AI Data\Aggression')));
  AddKV(sl, '  ', 'aiConfidence', ActorConfidenceValue(GetText(e, 'AIDT - AI Data\Confidence')));
  AddKV(sl, '  ', 'aiEnergyLevel', SafeInt(GetText(e, 'AIDT - AI Data\Energy Level'), '50'));
  AddKV(sl, '  ', 'aiMorality', ActorMoralityValue(GetText(e, 'AIDT - AI Data\Morality')));
  AddKV(sl, '  ', 'aiMood', ActorMoodValue(GetText(e, 'AIDT - AI Data\Mood')));
  AddKV(sl, '  ', 'aiAssistance', ActorAssistanceValue(GetText(e, 'AIDT - AI Data\Assistance')));
  AddKV(sl, '  ', 'aiAggroRadiusBehavior', YesNoBoolJson(GetText(e, 'AIDT - AI Data\Aggro Radius Behavior')));
  AddKV(sl, '  ', 'aiAggroRadiusWarn', SafeInt(GetText(e, 'AIDT - AI Data\Warn'), '0'));
  AddKV(sl, '  ', 'aiAggroRadiusWarnAndAttack', SafeInt(GetText(e, 'AIDT - AI Data\Warn/Attack'), '0'));
  AddKV(sl, '  ', 'aiAggroRadiusAttack', SafeInt(GetText(e, 'AIDT - AI Data\Attack'), '0'));
  AddKV(sl, '  ', 'aiNoSlowApproach', YesNoBoolJson(GetText(e, 'AIDT - AI Data\No Slow Approach')));
  AddFormRefArrayFromPaths(sl, 'headParts', e, 'Head Parts', 'Head Parts\Head Part', 'PNAM - Head Parts', '');
  AddRankedFormRefArrayFromPaths(sl, 'factions', e, 'Factions', 'SNAM - Factions', 'Faction', '');
  AddRankedFormRefArrayFromPaths(sl, 'perks', e, 'Perks', 'PRKR - Perks', 'Perk', '');
  AddFormRefArrayFromPaths(sl, 'spells', e, 'Spells', 'SPLO - Actor Effects', 'Actor Effects', 'Spell List');
  AddFormRefArrayFromPaths(sl, 'packages', e, 'Packages', 'Packages\Packages', 'AI Packages', '');
  sl.Add('  "skills": [15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15],');
  sl.Add('  "skillOffsets": [0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0],');
end;

procedure AddSoundDescriptorFields(sl: TStringList; e: IInterface);
begin
  sl.Add('  "soundFiles": [],');
  AddKV(sl, '  ', 'frequencyShift', SafeInt(GetText(e, 'BNAM - Values\% Frequency Shift'), '0'));
  AddKV(sl, '  ', 'frequencyVariance', SafeInt(GetText(e, 'BNAM - Values\% Frequency Variance'), '0'));
  AddKV(sl, '  ', 'priority', SafeInt(GetText(e, 'BNAM - Values\Priority'), '128'));
  AddKV(sl, '  ', 'dbVariance', SafeInt(GetText(e, 'BNAM - Values\db Variance'), '0'));
  AddKV(sl, '  ', 'staticAttenuation', SafeFloat(GetText(e, 'BNAM - Values\Static Attenuation (db)'), '0.0'));
  AddKV(sl, '  ', 'looping', '0');
  AddKV(sl, '  ', 'rumbleSendValue', SafeInt(GetText(e, 'LNAM - Values\Rumble Send Value'), '0'));
  sl.Add('  "conditions": [],');
end;

function ExportRecord(e: IInterface): Boolean;
var
  sl: TStringList;
  sig, kind, edid, fn: string;
begin
  Result := False;
  sig := Signature(e);
  kind := FormKindForSignature(sig);
  if kind = '' then begin
    AddMessage('[DFG] Unsupported signature ' + sig + ': ' + Name(e));
    Inc(SkippedCount);
    Exit;
  end;

  edid := SafeFileName(EditorIDOf(e));
  fn := OutputDir + edid + '.json';

  sl := TStringList.Create;
  try
    sl.Add('{');
    AddKV(sl, '  ', 'schemaVersion', '1');
    AddStringKV(sl, '  ', 'formKind', kind);
    AddStringKV(sl, '  ', 'sourceSignature', sig);
    AddStringKV(sl, '  ', 'editorId', edid);

    if sig = 'GLOB' then begin
      AddStringKV(sl, '  ', 'globalType', GlobalTypeFromRecord(e));
      AddKV(sl, '  ', 'defaultValue', SafeFloat(GetText(e, 'FLTV - Value'), '0.0'));
    end
    else if sig = 'OTFT' then AddOutfitFields(sl, e)
    else if sig = 'ARMA' then AddArmorTypeFields(sl, e)
    else if sig = 'ARMO' then AddArmorFields(sl, e)
    else if sig = 'CLFM' then AddColorFields(sl, e)
    else if sig = 'ARTO' then AddArtObjectFields(sl, e)
    else if sig = 'PERK' then AddPerkFields(sl, e)
    else if sig = 'HDPT' then AddHeadPartFields(sl, e)
    else if sig = 'SNDR' then AddSoundDescriptorFields(sl, e)
    else if sig = 'LIGH' then AddLightFields(sl, e)
    else if sig = 'EXPL' then AddExplosionFields(sl, e)
    else if sig = 'ACTI' then AddActivatorFields(sl, e)
    else if sig = 'EFSH' then AddEffectShaderFields(sl, e)
    else if sig = 'NPC_' then AddNPCFields(sl, e);

    RemoveTrailingComma(sl);
    sl.Add('}');
    sl.SaveToFile(fn);
    AddMessage('[DFG] Exported ' + sig + ' ' + Name(e) + ' -> ' + fn);
    Inc(ExportedCount);
    Result := True;
  finally
    sl.Free;
  end;
end;

function Initialize: Integer;
begin
  ExportedCount := 0;
  SkippedCount := 0;

  DebugNpcFlags := True;
  DebugNpcEditorID := 'HousecarlWhiterun';
  // Use DebugNpcEditorID := ''; to log every selected NPC.

  OutputDir := wbDataPath + 'Viny Mods\Dynamic Forms Generator\Forms\';
  ForceDirectories(OutputDir);
  AddMessage('[DFG] Output folder: ' + OutputDir);
  AddMessage('[DFG][FLAGS] Debug enabled=' + DebugBoolText(DebugNpcFlags) +
    ', NPC filter="' + DebugNpcEditorID + '"');
  Result := 0;
end;

function Process(e: IInterface): Integer;
begin
  if not Assigned(e) then begin
    Result := 0;
    Exit;
  end;

  ExportRecord(e);
  Result := 0;
end;

function Finalize: Integer;
begin
  AddMessage('[DFG] Done. Exported: ' + IntToStr(ExportedCount) + ', skipped: ' + IntToStr(SkippedCount));
  Result := 0;
end;

end.
