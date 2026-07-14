unit userscript;

var
  OutputDir: string;
  PackageName: string;
  PackageDir: string;
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

function IsSafePackageChar(const c: string): Boolean;
begin
  Result :=
    ((c >= 'A') and (c <= 'Z')) or
    ((c >= 'a') and (c <= 'z')) or
    ((c >= '0') and (c <= '9')) or
    (c = '_') or
    (c = '-') or
    (c = '.');
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

function SafePackageFolder(const s: string): string;
var
  i: Integer;
  c: string;
begin
  Result := '';
  for i := 1 to Length(s) do begin
    c := Copy(s, i, 1);
    if IsSafePackageChar(c) then
      Result := Result + c
    else
      Result := Result + '_';
  end;

  if Result = '' then
    Result := 'Local_Forms';
end;

procedure WritePackageManifest;
var
  sl: TStringList;
  fn: string;
begin
  fn := PackageDir + 'manifest.json';
  if FileExists(fn) then
    Exit;

  sl := TStringList.Create;
  try
    sl.Add('{');
    AddKV(sl, '  ', 'schemaVersion', '1');
    AddStringKV(sl, '  ', 'displayName', PackageName);
    AddKV(sl, '  ', 'enabled', 'true');
    AddKV(sl, '  ', 'priority', '0');
    AddStringKV(sl, '  ', 'database', 'package.db');
    RemoveTrailingComma(sl);
    sl.Add('}');
    sl.SaveToFile(fn);
  finally
    sl.Free;
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

function StructFieldText(e: IInterface; const structName, fieldName, fallback: string): string;
var
  container, field: IInterface;
begin
  Result := fallback;
  container := FindChildByNameContains(e, structName);
  if not Assigned(container) then
    Exit;

  field := FindDescendantByExactName(container, fieldName);
  if not Assigned(field) then
    field := FindDescendantByNameContains(container, fieldName);
  if Assigned(field) then
    Result := GetEditValue(field);
  if Result = '' then
    Result := fallback;
end;

function StructFieldElement(e: IInterface; const structName, fieldName: string): IInterface;
var
  container: IInterface;
begin
  Result := nil;
  container := FindChildByNameContains(e, structName);
  if not Assigned(container) then
    Exit;

  Result := FindDescendantByExactName(container, fieldName);
  if not Assigned(Result) then
    Result := FindDescendantByNameContains(container, fieldName);
end;

function StructFieldLinked(e: IInterface; const structName, fieldName: string): IInterface;
var
  field: IInterface;
begin
  Result := nil;
  field := StructFieldElement(e, structName, fieldName);
  if Assigned(field) then
    Result := LinksTo(field);
  if not Assigned(Result) and Assigned(field) then
    Result := FirstLinkedInElement(field);
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
  else if sig = 'FLST' then Result := 'FormList'
  else if sig = 'EQUP' then Result := 'EquipSlot'
  else if sig = 'VTYP' then Result := 'VoiceType'
  else if sig = 'OTFT' then Result := 'Outfit'
  else if sig = 'ARMA' then Result := 'ArmorType'
  else if sig = 'ARMO' then Result := 'Armor'
  else if sig = 'BOOK' then Result := 'Book'
  else if sig = 'MISC' then Result := 'Misc'
  else if sig = 'KEYM' then Result := 'Key'
  else if sig = 'SLGM' then Result := 'SoulGem'
  else if sig = 'MATT' then Result := 'MaterialType'
  else if sig = 'AMMO' then Result := 'Ammo'
  else if sig = 'WEAP' then Result := 'Weapon'
  else if sig = 'ALCH' then Result := 'AlchemyItem'
  else if sig = 'INGR' then Result := 'Ingredient'
  else if sig = 'SPEL' then Result := 'Spell'
  else if sig = 'ENCH' then Result := 'Enchantment'
  else if sig = 'SCRL' then Result := 'Scroll'
  else if sig = 'PROJ' then Result := 'Projectile'
  else if sig = 'TXST' then Result := 'TextureSet'
  else if sig = 'HAZD' then Result := 'Hazard'
  else if sig = 'IPCT' then Result := 'ImpactData'
  else if sig = 'RFCT' then Result := 'ReferenceEffect'
  else if sig = 'DUAL' then Result := 'DualCastData'
  else if sig = 'STAT' then Result := 'Static'
  else if sig = 'MSTT' then Result := 'MovableStatic'
  else if sig = 'DOOR' then Result := 'Door'
  else if sig = 'CSTY' then Result := 'CombatStyle'
  else if sig = 'SNCT' then Result := 'SoundCategory'
  else if sig = 'CLAS' then Result := 'Class'
  else if sig = 'FLOR' then Result := 'Flora'
  else if sig = 'TREE' then Result := 'Tree'
  else if sig = 'COBJ' then Result := 'ConstructibleObject'
  else if sig = 'CONT' then Result := 'Container'
  else if sig = 'IPDS' then Result := 'ImpactDataSet'
  else if sig = 'COLL' then Result := 'CollisionLayer'
  else if sig = 'FSTP' then Result := 'Footstep'
  else if sig = 'FSTS' then Result := 'FootstepSet'
  else if sig = 'REVB' then Result := 'ReverbParameters'
  else if sig = 'ASPC' then Result := 'AcousticSpace'
  else if sig = 'APPA' then Result := 'Apparatus'
  else if sig = 'SCOL' then Result := 'StaticCollection'
  else if sig = 'GRAS' then Result := 'Grass'
  else if sig = 'IDLM' then Result := 'IdleMarker'
  else if sig = 'ECZN' then Result := 'EncounterZone'
  else if sig = 'RELA' then Result := 'Relationship'
  else if sig = 'ASTP' then Result := 'AssociationType'
  else if sig = 'MOVT' then Result := 'MovementType'
  else if sig = 'WOOP' then Result := 'WordOfPower'
  else if sig = 'WATR' then Result := 'Water'
  else if sig = 'IMGS' then Result := 'ImageSpace'
  else if sig = 'LGTM' then Result := 'LightingTemplate'
  else if sig = 'SHOU' then Result := 'Shout'
  else if sig = 'LVLI' then Result := 'LeveledItem'
  else if sig = 'LVLN' then Result := 'LeveledNPC'
  else if sig = 'LVSP' then Result := 'LeveledSpell'
  else if sig = 'LCRT' then Result := 'LocationRefType'
  else if sig = 'AACT' then Result := 'Action'
  else if sig = 'MICN' then Result := 'MenuIcon'
  else if sig = 'EYES' then Result := 'Eyes'
  else if sig = 'NOTE' then Result := 'Note'
  else if sig = 'ANIO' then Result := 'AnimatedObject'
  else if sig = 'LSCR' then Result := 'LoadScreen'
  else if sig = 'SPGD' then Result := 'ShaderParticleGeometry'
  else if sig = 'ADDN' then Result := 'AddonNode'
  else if sig = 'MGEF' then Result := 'MagicEffect'
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

function SpellTypeFromRecord(e: IInterface): string;
var
  t: string;
begin
  t := LowerCase(StructFieldText(e, 'SPIT', 'Type', ''));
  if Pos('voice', t) > 0 then Result := '11'
  else if Pos('lesser', t) > 0 then Result := '3'
  else if Pos('power', t) > 0 then Result := '2'
  else if Pos('disease', t) > 0 then Result := '1'
  else if Pos('ability', t) > 0 then Result := '4'
  else if Pos('poison', t) > 0 then Result := '5'
  else if Pos('staff', t) > 0 then Result := '12'
  else if Pos('enchant', t) > 0 then Result := '6'
  else if Pos('potion', t) > 0 then Result := '7'
  else if Pos('alchemy', t) > 0 then Result := '7'
  else if Pos('ingredient', t) > 0 then Result := '8'
  else if Pos('wort', t) > 0 then Result := '8'
  else if Pos('leveled', t) > 0 then Result := '9'
  else if Pos('addiction', t) > 0 then Result := '10'
  else if Pos('scroll', t) > 0 then Result := '13'
  else Result := JsonInt(t, '0');
end;

function SpellCastingTypeFromRecord(e: IInterface): string;
var
  t: string;
begin
  t := LowerCase(StructFieldText(e, 'SPIT', 'Cast Type', ''));
  if Pos('constant', t) > 0 then Result := '0'
  else if Pos('concentration', t) > 0 then Result := '2'
  else if Pos('scroll', t) > 0 then Result := '3'
  else if Pos('fire', t) > 0 then Result := '1'
  else Result := JsonInt(t, '1');
end;

function SpellDeliveryFromRecord(e: IInterface): string;
var
  t: string;
begin
  t := LowerCase(StructFieldText(e, 'SPIT', 'Delivery', ''));
  if Pos('touch', t) > 0 then Result := '1'
  else if Pos('aimed', t) > 0 then Result := '2'
  else if Pos('target actor', t) > 0 then Result := '3'
  else if Pos('target location', t) > 0 then Result := '4'
  else if Pos('none', t) > 0 then Result := '5'
  else if Pos('self', t) > 0 then Result := '0'
  else Result := JsonInt(t, '0');
end;

function SpellFlagsFromRecord(e: IInterface): string;
var
  flags: string;
  value: Int64;
begin
  flags := StructFieldText(e, 'SPIT', 'Flags', '');
  value := 0;
  if HasWord(flags, 'Cost Override') then value := value + 1;
  if HasWord(flags, 'Food Item') then value := value + 2;
  if HasWord(flags, 'Extend Duration') then value := value + 8;
  if HasWord(flags, 'PC Start') then value := value + 131072;
  if HasWord(flags, 'Instant Cast') then value := value + 262144;
  if HasWord(flags, 'Ignore LOS') then value := value + 524288;
  if HasWord(flags, 'Ignore Resistance') then value := value + 1048576;
  if HasWord(flags, 'No Absorb') then value := value + 2097152;
  if HasWord(flags, 'No Dual Cast') then value := value + 8388608;
  Result := IntToStr(value);
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


procedure AddSpellMagicEffects(sl: TStringList; e: IInterface);
var
  arr, row, linked: IInterface;
  i: Integer;
  magnitude, area, duration: string;
begin
  sl.Add('  "magicEffectsOverride": true,');
  sl.Add('  "magicEffects": [');
  arr := FirstElementByPath(e, 'Effects', 'Effects (sorted)', 'Effects\Effect', '');
  if Assigned(arr) then begin
    for i := 0 to ElementCount(arr) - 1 do begin
      row := ElementByIndex(arr, i);
      linked := LinkedByPath(row, 'EFID - Base Effect');
      if not Assigned(linked) then
        linked := LinkedByPath(row, 'EFID');
      if not Assigned(linked) then
        linked := FirstLinkedInElement(row);
      if not Assigned(linked) then
        Continue;

      magnitude := GetFirstText(row, 'EFIT\Magnitude', 'EFIT - EFIT\Magnitude', 'EFIT - Effect Data\Magnitude', 'Magnitude', '0.0');
      area := GetFirstText(row, 'EFIT\Area', 'EFIT - EFIT\Area', 'EFIT - Effect Data\Area', 'Area', '0');
      duration := GetFirstText(row, 'EFIT\Duration', 'EFIT - EFIT\Duration', 'EFIT - Effect Data\Duration', 'Duration', '0');
      sl.Add('    {');
      AddKV(sl, '      ', 'effectSetting', FormRefJson(linked));
      AddKV(sl, '      ', 'magnitude', JsonFloat(magnitude, '0.0'));
      AddKV(sl, '      ', 'area', JsonInt(area, '0'));
      AddKV(sl, '      ', 'duration', JsonInt(duration, '0'));
      AddKV(sl, '      ', 'cost', '0.0');
      AddPerkConditionsArray(sl, 'conditions', row, '      ');
      RemoveTrailingComma(sl);
      sl.Add('    },');
    end;
  end;
  RemoveTrailingComma(sl);
  sl.Add('  ],');
end;

procedure AddSpellFields(sl: TStringList; e: IInterface);
var
  spellType, costOverride, chargeTime, castingType, delivery, castDuration, spellRange: string;
begin
  spellType := SpellTypeFromRecord(e);
  costOverride := JsonInt(StructFieldText(e, 'SPIT', 'Cost', '0'), '0');
  chargeTime := JsonFloat(StructFieldText(e, 'SPIT', 'Charge Time', '0.0'), '0.0');
  castingType := SpellCastingTypeFromRecord(e);
  delivery := SpellDeliveryFromRecord(e);
  castDuration := JsonFloat(StructFieldText(e, 'SPIT', 'Cast Duration', '0.0'), '0.0');
  spellRange := JsonFloat(StructFieldText(e, 'SPIT', 'Range', '0.0'), '0.0');

  AddMessage('[DFG][SPEL] ' + EditorIDOf(e) +
    ' type=' + spellType +
    ' cost=' + costOverride +
    ' charge=' + chargeTime +
    ' casting=' + castingType +
    ' delivery=' + delivery +
    ' duration=' + castDuration +
    ' range=' + spellRange);

  AddStringKV(sl, '  ', 'fullName', GetText(e, 'FULL - Name'));
  AddStringKV(sl, '  ', 'description', GetText(e, 'DESC - Description'));
  AddFormRefArrayFromElement(sl, 'keywords', ElementByPath(e, 'KWDA - Keywords'));
  AddFormRefKV(sl, 'equipSlot', LinkedByPath(e, 'ETYP - Equipment Type'));
  AddFormRefKV(sl, 'castingPerk', LinkedByPath(e, 'SPIT - Data\Casting Perk'));
  AddFormRefKV(sl, 'menuDisplayObject', LinkedByPath(e, 'MDOB - Menu Display Object'));
  AddKV(sl, '  ', 'spellFlags', SpellFlagsFromRecord(e));
  AddKV(sl, '  ', 'spellType', spellType);
  AddKV(sl, '  ', 'spellCostOverride', costOverride);
  AddKV(sl, '  ', 'spellChargeTime', chargeTime);
  AddKV(sl, '  ', 'spellCastingType', castingType);
  AddKV(sl, '  ', 'spellDelivery', delivery);
  AddKV(sl, '  ', 'spellCastDuration', castDuration);
  AddKV(sl, '  ', 'spellRange', spellRange);
  AddSpellMagicEffects(sl, e);
end;

procedure AddProjectileFields(sl: TStringList; e: IInterface);
begin
  AddStringKV(sl, '  ', 'fullName', GetText(e, 'FULL - Name'));
  AddStringKV(sl, '  ', 'modelPath', GetText(e, 'Model\MODL - Model FileName'));
  AddStringKV(sl, '  ', 'projectileMuzzleFlashModel', FirstText(e, 'NAM1 - Muzzle Flash Model\MODL - Model FileName', 'Muzzle Flash Model\MODL - Model FileName', 'NAM1\MODL - Model FileName'));
  AddKV(sl, '  ', 'projectileFlags', ElementIntegerText(StructFieldElement(e, 'DATA', 'Flags'), '0'));
  AddKV(sl, '  ', 'projectileTypes', ElementIntegerText(StructFieldElement(e, 'DATA', 'Type'), '1'));
  AddKV(sl, '  ', 'projectileGravity', JsonFloat(StructFieldText(e, 'DATA', 'Gravity', '0.0'), '0.0'));
  AddKV(sl, '  ', 'projectileSpeed', JsonFloat(StructFieldText(e, 'DATA', 'Speed', '1000.0'), '1000.0'));
  AddKV(sl, '  ', 'projectileRange', JsonFloat(StructFieldText(e, 'DATA', 'Range', '10000.0'), '10000.0'));
  AddFormRefKV(sl, 'projectileLight', StructFieldLinked(e, 'DATA', 'Light'));
  AddFormRefKV(sl, 'projectileMuzzleFlashLight', StructFieldLinked(e, 'DATA', 'Muzzle Flash - Light'));
  AddKV(sl, '  ', 'projectileTracerChance', JsonFloat(StructFieldText(e, 'DATA', 'Tracer Chance', '0.0'), '0.0'));
  AddKV(sl, '  ', 'projectileExplosionProximity', JsonFloat(StructFieldText(e, 'DATA', 'Explosion - Proximity', '0.0'), '0.0'));
  AddKV(sl, '  ', 'projectileExplosionTimer', JsonFloat(StructFieldText(e, 'DATA', 'Explosion - Timer', '0.0'), '0.0'));
  AddFormRefKV(sl, 'projectileExplosionType', StructFieldLinked(e, 'DATA', 'Explosion'));
  AddFormRefKV(sl, 'projectileActiveSoundLoop', StructFieldLinked(e, 'DATA', 'Sound'));
  AddKV(sl, '  ', 'projectileMuzzleFlashDuration', JsonFloat(StructFieldText(e, 'DATA', 'Muzzle Flash - Duration', '0.0'), '0.0'));
  AddKV(sl, '  ', 'projectileFadeOutTime', JsonFloat(StructFieldText(e, 'DATA', 'Fade Duration', '0.0'), '0.0'));
  AddKV(sl, '  ', 'projectileForce', JsonFloat(StructFieldText(e, 'DATA', 'Impact Force', '0.0'), '0.0'));
  AddFormRefKV(sl, 'projectileCountdownSound', StructFieldLinked(e, 'DATA', 'Sound - Countdown'));
  AddFormRefKV(sl, 'projectileDeactivateSound', StructFieldLinked(e, 'DATA', 'Sound - Disable'));
  AddFormRefKV(sl, 'projectileDefaultWeaponSource', StructFieldLinked(e, 'DATA', 'Default Weapon Source'));
  AddKV(sl, '  ', 'projectileConeSpread', JsonFloat(StructFieldText(e, 'DATA', 'Cone Spread', '0.0'), '0.0'));
  AddKV(sl, '  ', 'projectileCollisionRadius', JsonFloat(StructFieldText(e, 'DATA', 'Collision Radius', '0.0'), '0.0'));
  AddKV(sl, '  ', 'projectileLifetime', JsonFloat(StructFieldText(e, 'DATA', 'Lifetime', '0.0'), '0.0'));
  AddKV(sl, '  ', 'projectileRelaunchInterval', JsonFloat(StructFieldText(e, 'DATA', 'Relaunch Interval', '0.0'), '0.0'));
  AddFormRefKV(sl, 'projectileDecalData', StructFieldLinked(e, 'DATA', 'Decal Data'));
  AddFormRefKV(sl, 'projectileCollisionLayer', StructFieldLinked(e, 'DATA', 'Collision Layer'));
  AddKV(sl, '  ', 'projectileSoundLevel', ElementIntegerText(FirstElementByPath(e, 'VNAM - Sound Level', 'VNAM', 'Sound Level', ''), '1'));
end;

procedure AddScrollFields(sl: TStringList; e: IInterface);
begin
  AddStringKV(sl, '  ', 'fullName', GetText(e, 'FULL - Name'));
  AddStringKV(sl, '  ', 'description', GetText(e, 'DESC - Description'));
  AddStringKV(sl, '  ', 'modelPath', GetText(e, 'Model\MODL - Model FileName'));
  AddKV(sl, '  ', 'itemValue', JsonInt(GetFirstText(e, 'DATA\Value', 'DATA - Data\Value', 'Value', '', '0'), '0'));
  AddKV(sl, '  ', 'itemWeight', JsonFloat(GetFirstText(e, 'DATA\Weight', 'DATA - Data\Weight', 'Weight', '', '0.0'), '0.0'));
  AddFormRefArrayFromElement(sl, 'keywords', ElementByPath(e, 'KWDA - Keywords'));
  AddFormRefKV(sl, 'equipSlot', LinkedByPath(e, 'ETYP - Equipment Type'));
  AddFormRefKV(sl, 'menuDisplayObject', LinkedByPath(e, 'MDOB - Menu Display Object'));
  AddFormRefKV(sl, 'pickupSound', LinkedByPath(e, 'YNAM - Pick Up Sound'));
  AddFormRefKV(sl, 'putdownSound', LinkedByPath(e, 'ZNAM - Put Down Sound'));
  AddKV(sl, '  ', 'scrollFlags', SpellFlagsFromRecord(e));
  AddKV(sl, '  ', 'scrollCostOverride', JsonInt(StructFieldText(e, 'SPIT', 'Cost', '0'), '0'));
  AddKV(sl, '  ', 'scrollChargeTime', JsonFloat(StructFieldText(e, 'SPIT', 'Charge Time', '0.0'), '0.0'));
  AddKV(sl, '  ', 'scrollDelivery', ElementIntegerText(StructFieldElement(e, 'SPIT', 'Delivery'), '0'));
  AddKV(sl, '  ', 'scrollCastDuration', JsonFloat(StructFieldText(e, 'SPIT', 'Cast Duration', '0.0'), '0.0'));
  AddKV(sl, '  ', 'scrollRange', JsonFloat(StructFieldText(e, 'SPIT', 'Range', '0.0'), '0.0'));
  AddFormRefKV(sl, 'scrollCastingPerk', StructFieldLinked(e, 'SPIT', 'Casting Perk'));
  AddSpellMagicEffects(sl, e);
end;

procedure AddEnchantmentFields(sl: TStringList; e: IInterface);
begin
  AddStringKV(sl, '  ', 'fullName', GetText(e, 'FULL - Name'));
  AddFormRefArrayFromElement(sl, 'keywords', ElementByPath(e, 'KWDA - Keywords'));
  AddKV(sl, '  ', 'enchantmentFlags', ElementIntegerText(StructFieldElement(e, 'ENIT', 'Flags'), '0'));
  AddKV(sl, '  ', 'enchantmentCostOverride', JsonInt(StructFieldText(e, 'ENIT', 'Enchantment Cost', '0'), '0'));
  AddKV(sl, '  ', 'enchantmentCastingType', ElementIntegerText(StructFieldElement(e, 'ENIT', 'Cast Type'), '1'));
  AddKV(sl, '  ', 'enchantmentChargeOverride', JsonInt(StructFieldText(e, 'ENIT', 'Enchantment Amount', '0'), '0'));
  AddKV(sl, '  ', 'enchantmentDelivery', ElementIntegerText(StructFieldElement(e, 'ENIT', 'Target Type'), '0'));
  AddKV(sl, '  ', 'enchantmentSpellType', ElementIntegerText(StructFieldElement(e, 'ENIT', 'Enchant Type'), '6'));
  AddKV(sl, '  ', 'enchantmentChargeTime', JsonFloat(StructFieldText(e, 'ENIT', 'Charge Time', '0.0'), '0.0'));
  AddFormRefKV(sl, 'baseEnchantment', StructFieldLinked(e, 'ENIT', 'Base Enchantment'));
  AddFormRefKV(sl, 'wornRestrictions', StructFieldLinked(e, 'ENIT', 'Worn Restrictions'));
  AddSpellMagicEffects(sl, e);
end;

procedure AddMagicEffectFields(sl: TStringList; e: IInterface);
var
  dataElement, sounds, row, typeElement, soundElement: IInterface;
  soundRefs: array[0..5] of IInterface;
  i, soundId: Integer;
begin
  AddStringKV(sl, '  ', 'fullName', GetText(e, 'FULL - Name'));
  AddStringKV(sl, '  ', 'magicItemDescription', GetText(e, 'DNAM - Magic Item Description'));
  AddFormRefArrayFromElement(sl, 'keywords', ElementByPath(e, 'KWDA - Keywords'));
  AddFormRefKV(sl, 'menuDisplayObject', LinkedByPath(e, 'MDOB - Menu Display Object'));

  dataElement := FindChildByNameContains(e, 'DATA - Magic Effect Data');
  if not Assigned(dataElement) then
    dataElement := FindChildByNameContains(e, 'DATA');

  AddKV(sl, '  ', 'magicEffectFlags', ElementIntegerText(StructFieldElement(e, 'DATA - Magic Effect Data', 'Flags'), '0'));
  AddKV(sl, '  ', 'magicEffectBaseCost', JsonFloat(StructFieldText(e, 'DATA - Magic Effect Data', 'Base Cost', '0.0'), '0.0'));
  AddFormRefKV(sl, 'magicEffectAssociatedForm', StructFieldLinked(e, 'DATA - Magic Effect Data', 'Assoc. Item'));
  AddKV(sl, '  ', 'magicEffectAssociatedSkill', ElementIntegerText(StructFieldElement(e, 'DATA - Magic Effect Data', 'Magic Skill'), '-1'));
  AddKV(sl, '  ', 'magicEffectResistVariable', ElementIntegerText(StructFieldElement(e, 'DATA - Magic Effect Data', 'Resist Value'), '-1'));
  AddFormRefArrayFromElement(sl, 'magicEffectCounterEffects', FirstElementByPath(e, 'ESCE - Counter Effects', 'Counter Effects', 'ESCE', ''));
  AddFormRefKV(sl, 'magicEffectLight', StructFieldLinked(e, 'DATA - Magic Effect Data', 'Casting Light'));
  AddKV(sl, '  ', 'magicEffectTaperWeight', JsonFloat(StructFieldText(e, 'DATA - Magic Effect Data', 'Taper Weight', '0.0'), '0.0'));
  AddFormRefKV(sl, 'magicEffectShader', StructFieldLinked(e, 'DATA - Magic Effect Data', 'Hit Shader'));
  AddFormRefKV(sl, 'magicEffectEnchantShader', StructFieldLinked(e, 'DATA - Magic Effect Data', 'Enchant Shader'));
  AddKV(sl, '  ', 'magicEffectMinimumSkill', JsonInt(StructFieldText(e, 'DATA - Magic Effect Data', 'Minimum Skill Level', '0'), '0'));
  AddKV(sl, '  ', 'magicEffectSpellmakingArea', JsonInt(StructFieldText(e, 'DATA - Magic Effect Data', 'Spellmaking Area', '0'), '0'));
  AddKV(sl, '  ', 'magicEffectSpellmakingChargeTime', JsonFloat(StructFieldText(e, 'DATA - Magic Effect Data', 'Spellmaking Charge Time', '0.0'), '0.0'));
  AddKV(sl, '  ', 'magicEffectTaperCurve', JsonFloat(StructFieldText(e, 'DATA - Magic Effect Data', 'Taper Curve', '0.0'), '0.0'));
  AddKV(sl, '  ', 'magicEffectTaperDuration', JsonFloat(StructFieldText(e, 'DATA - Magic Effect Data', 'Taper Duration', '0.0'), '0.0'));
  AddKV(sl, '  ', 'magicEffectSecondAVWeight', JsonFloat(StructFieldText(e, 'DATA - Magic Effect Data', 'Second AV Weight', '0.0'), '0.0'));
  AddKV(sl, '  ', 'magicEffectArchetype', ElementIntegerText(StructFieldElement(e, 'DATA - Magic Effect Data', 'Archetype'), '0'));
  AddKV(sl, '  ', 'magicEffectPrimaryAV', ElementIntegerText(StructFieldElement(e, 'DATA - Magic Effect Data', 'Actor Value'), '-1'));
  AddFormRefKV(sl, 'magicEffectProjectile', StructFieldLinked(e, 'DATA - Magic Effect Data', 'Projectile'));
  AddFormRefKV(sl, 'magicEffectExplosion', StructFieldLinked(e, 'DATA - Magic Effect Data', 'Explosion'));
  AddKV(sl, '  ', 'magicEffectCastingType', ElementIntegerText(StructFieldElement(e, 'DATA - Magic Effect Data', 'Casting Type'), '1'));
  AddKV(sl, '  ', 'magicEffectDelivery', ElementIntegerText(StructFieldElement(e, 'DATA - Magic Effect Data', 'Delivery'), '0'));
  AddKV(sl, '  ', 'magicEffectSecondaryAV', ElementIntegerText(StructFieldElement(e, 'DATA - Magic Effect Data', 'Second Actor Value'), '-1'));
  AddFormRefKV(sl, 'magicEffectCastingArt', StructFieldLinked(e, 'DATA - Magic Effect Data', 'Casting Art'));
  AddFormRefKV(sl, 'magicEffectHitEffectArt', StructFieldLinked(e, 'DATA - Magic Effect Data', 'Hit Effect Art'));
  AddFormRefKV(sl, 'magicEffectImpactDataSet', StructFieldLinked(e, 'DATA - Magic Effect Data', 'Impact Data'));
  AddKV(sl, '  ', 'magicEffectSkillUsageMult', JsonFloat(StructFieldText(e, 'DATA - Magic Effect Data', 'Skill Usage Mult', '0.0'), '0.0'));
  AddFormRefKV(sl, 'magicEffectDualCastData', StructFieldLinked(e, 'DATA - Magic Effect Data', 'Dual Casting'));
  AddKV(sl, '  ', 'magicEffectDualCastScale', JsonFloat(StructFieldText(e, 'DATA - Magic Effect Data', 'Dual Cast Scale', '1.0'), '1.0'));
  AddFormRefKV(sl, 'magicEffectEnchantEffectArt', StructFieldLinked(e, 'DATA - Magic Effect Data', 'Enchant Art'));
  AddFormRefKV(sl, 'magicEffectHitVisuals', StructFieldLinked(e, 'DATA - Magic Effect Data', 'Hit Visuals'));
  AddFormRefKV(sl, 'magicEffectEnchantVisuals', StructFieldLinked(e, 'DATA - Magic Effect Data', 'Enchant Visuals'));
  AddFormRefKV(sl, 'magicEffectEquipAbility', StructFieldLinked(e, 'DATA - Magic Effect Data', 'Equip Ability'));
  AddFormRefKV(sl, 'magicEffectImageSpaceMod', StructFieldLinked(e, 'DATA - Magic Effect Data', 'Image Space Modifier'));
  AddFormRefKV(sl, 'magicEffectPerk', StructFieldLinked(e, 'DATA - Magic Effect Data', 'Perk to Apply'));
  AddKV(sl, '  ', 'magicEffectCastingSoundLevel', ElementIntegerText(StructFieldElement(e, 'DATA - Magic Effect Data', 'Casting Sound Level'), '1'));
  AddKV(sl, '  ', 'magicEffectAIScore', JsonFloat(StructFieldText(e, 'DATA - Magic Effect Data', 'Script Effect AI Score', '0.0'), '0.0'));
  AddKV(sl, '  ', 'magicEffectAIDelayTime', JsonFloat(StructFieldText(e, 'DATA - Magic Effect Data', 'Script Effect AI Delay Time', '0.0'), '0.0'));

  for i := 0 to 5 do
    soundRefs[i] := nil;
  sounds := FirstElementByPath(e, 'SNDD - Sounds', 'Sounds', 'SNDD', '');
  if Assigned(sounds) then begin
    for i := 0 to ElementCount(sounds) - 1 do begin
      row := ElementByIndex(sounds, i);
      typeElement := FindDescendantByExactName(row, 'Type');
      soundElement := FindDescendantByExactName(row, 'Sound');
      soundId := NativeIntOfElement(typeElement, -1);
      if (soundId >= 0) and (soundId <= 5) and Assigned(soundElement) then
        soundRefs[soundId] := LinksTo(soundElement);
    end;
  end;
  for i := 0 to 5 do
    if Assigned(soundRefs[i]) then
      AddFormRefKV(sl, 'magicEffectSound' + IntToStr(i), soundRefs[i]);

  AddPerkConditionsArray(sl, 'conditions', FirstElementByPath(e, 'Conditions', 'Conditions (sorted)', 'CTDA - Conditions', ''));
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

function BookTypeFromRecord(e: IInterface): string;
var
  t: string;
begin
  t := LowerCase(GetFirstText(e, 'DATA - Data\Type', 'DATA\Type', 'Type', '', ''));
  if Pos('note', t) > 0 then Result := '1'
  else if Pos('scroll', t) > 0 then Result := '1'
  else Result := '0';
end;

function BookFlagsFromRecord(e: IInterface): string;
var
  flags: string;
  value: Int64;
begin
  flags := GetFirstText(e, 'DATA - Data\Flags', 'DATA\Flags', 'Flags', '', '');
  value := 0;
  if HasWord(flags, 'Teaches Skill') then value := value + 1;
  if HasWord(flags, 'Can''t be taken') then value := value + 2;
  if HasWord(flags, 'Cant be taken') then value := value + 2;
  if HasWord(flags, 'Teaches Spell') then value := value + 4;
  Result := IntToStr(value);
end;

function AmmoFlagsFromRecord(e: IInterface): string;
var
  flags: string;
  value: Int64;
begin
  flags := GetFirstText(e, 'DATA - Data\Flags', 'DATA\Flags', 'Flags', '', '');
  value := 0;
  if HasWord(flags, 'Ignores Normal Weapon Resistance') then value := value + 1;
  if HasWord(flags, 'Non-Playable') then value := value + 2;
  if HasWord(flags, 'Non-Bolt') then value := value + 4;
  Result := IntToStr(value);
end;

function AlchemyFlagsFromRecord(e: IInterface): string;
var
  flags: string;
  value: Int64;
begin
  flags := GetFirstText(e, 'ENIT - Effect Data\Flags', 'ENIT\Flags', 'Flags', '', '');
  value := 0;
  if HasWord(flags, 'Cost Override') then value := value + 1;
  if HasWord(flags, 'Food Item') then value := value + 2;
  if HasWord(flags, 'Extend Duration') then value := value + 8;
  if HasWord(flags, 'Medicine') then value := value + 65536;
  if HasWord(flags, 'Poison') then value := value + 131072;
  Result := IntToStr(value);
end;

function IngredientFlagsFromRecord(e: IInterface): string;
var
  flags: string;
  value: Int64;
begin
  flags := GetFirstText(e, 'ENIT - Effect Data\Flags', 'ENIT\Flags', 'Flags', '', '');
  value := 0;
  if HasWord(flags, 'Cost Override') then value := value + 1;
  if HasWord(flags, 'Food Item') then value := value + 2;
  if HasWord(flags, 'Extend Duration') then value := value + 8;
  if HasWord(flags, 'References Persist') then value := value + 256;
  Result := IntToStr(value);
end;

function WeaponTypeFromRecord(e: IInterface): string;
var
  t: string;
begin
  t := LowerCase(GetFirstText(e, 'DNAM - Data\Animation Type', 'DNAM\Animation Type', 'Animation Type', '', ''));
  if Pos('dagger', t) > 0 then Result := '2'
  else if Pos('war axe', t) > 0 then Result := '3'
  else if Pos('mace', t) > 0 then Result := '4'
  else if Pos('greatsword', t) > 0 then Result := '5'
  else if Pos('battleaxe', t) > 0 then Result := '6'
  else if Pos('bow', t) > 0 then Result := '7'
  else if Pos('staff', t) > 0 then Result := '8'
  else if Pos('crossbow', t) > 0 then Result := '9'
  else if Pos('sword', t) > 0 then Result := '1'
  else Result := '1';
end;

procedure AddCommonItemFields(sl: TStringList; e: IInterface);
begin
  AddStringKV(sl, '  ', 'fullName', GetText(e, 'FULL - Name'));
  AddStringKV(sl, '  ', 'modelPath', GetText(e, 'Model\MODL - Model FileName'));
  AddKV(sl, '  ', 'itemValue', SafeInt(FirstText(e, 'DATA - DATA\Value', 'DATA - Data\Value', 'DATA\Value'), '0'));
  AddKV(sl, '  ', 'itemWeight', SafeFloat(FirstText(e, 'DATA - DATA\Weight', 'DATA - Data\Weight', 'DATA\Weight'), '0.0'));
  AddStringKV(sl, '  ', 'inventoryIcon', FirstText(e, 'ICON - Inventory Image', 'ICON - Male inventory image', ''));
  AddStringKV(sl, '  ', 'messageIcon', FirstText(e, 'MICO - Message Icon', 'MICO - Male message icon', ''));
  AddFormRefKV(sl, 'pickupSound', LinkedByPath(e, 'YNAM - Pick Up Sound'));
  AddFormRefKV(sl, 'putdownSound', LinkedByPath(e, 'ZNAM - Put Down Sound'));
  AddFormRefArrayFromElement(sl, 'keywords', ElementByPath(e, 'KWDA - Keywords'));
end;

procedure AddFormListFields(sl: TStringList; e: IInterface);
begin
  AddFormRefArrayFromElement(sl, 'formListItems', ElementByPath(e, 'FormIDs'));
end;

procedure AddEquipSlotFields(sl: TStringList; e: IInterface);
begin
  AddFormRefArrayFromElement(sl, 'equipSlotParents', ElementByPath(e, 'PNAM - Parent Slots'));
  AddKV(sl, '  ', 'equipSlotFlags', SafeInt(GetFirstText(e, 'DATA - Flags', 'DATA\Flags', 'Flags', '', '0'), '0'));
end;

procedure AddVoiceTypeFields(sl: TStringList; e: IInterface);
var
  flags: string;
begin
  flags := GetFirstText(e, 'DNAM - Flags', 'DATA - Flags', 'Flags', '', '');
  AddKV(sl, '  ', 'voiceTypeAllowDefaultDialogue', BoolJson(not HasWord(flags, 'Allow Default Dialogue: False')));
  AddKV(sl, '  ', 'voiceTypeFemale', BoolJson(HasWord(flags, 'Female')));
end;

procedure AddBookFields(sl: TStringList; e: IInterface);
begin
  AddCommonItemFields(sl, e);
  AddStringKV(sl, '  ', 'description', GetText(e, 'CNAM - Description'));
  AddKV(sl, '  ', 'bookFlags', BookFlagsFromRecord(e));
  AddKV(sl, '  ', 'bookType', BookTypeFromRecord(e));
  AddFormRefKV(sl, 'teachesSpell', LinkedByPath(e, 'DATA - Data\Teaches Spell'));
  AddKV(sl, '  ', 'teachesActorValue', SafeInt(GetFirstText(e, 'DATA - Data\Skill', 'DATA\Skill', 'Skill', '', '-1'), '-1'));
end;

procedure AddMiscFields(sl: TStringList; e: IInterface);
begin
  AddCommonItemFields(sl, e);
end;

procedure AddSoulGemFields(sl: TStringList; e: IInterface);
begin
  AddCommonItemFields(sl, e);
  AddFormRefKV(sl, 'linkedSoulGem', LinkedByPath(e, 'NAM0 - Linked To'));
  AddKV(sl, '  ', 'currentSoul', SafeInt(GetFirstText(e, 'SOUL - Current Soul', 'Current Soul', '', '', '0'), '0'));
  AddKV(sl, '  ', 'soulCapacity', SafeInt(GetFirstText(e, 'SLCP - Soul Capacity', 'Soul Capacity', '', '', '0'), '0'));
end;

procedure AddMaterialTypeFields(sl: TStringList; e: IInterface);
begin
  AddStringKV(sl, '  ', 'materialName', GetText(e, 'MNAM - Material Name'));
  AddFormRefKV(sl, 'materialParent', LinkedByPath(e, 'PNAM - Parent Material'));
  AddFormRefKV(sl, 'havokImpactDataSet', LinkedByPath(e, 'HNAM - Havok Impact Data Set'));
  AddKV(sl, '  ', 'materialId', SafeInt(GetFirstText(e, 'DATA - Material ID', 'DATA\Material ID', 'Material ID', '', '0'), '0'));
  AddKV(sl, '  ', 'red', ColorChannel(e, 'CNAM - Color\Red', '255'));
  AddKV(sl, '  ', 'green', ColorChannel(e, 'CNAM - Color\Green', '255'));
  AddKV(sl, '  ', 'blue', ColorChannel(e, 'CNAM - Color\Blue', '255'));
  AddKV(sl, '  ', 'buoyancy', SafeFloat(GetFirstText(e, 'BNAM - Buoyancy', 'Buoyancy', '', '', '0.0'), '0.0'));
  AddKV(sl, '  ', 'flags', SafeInt(GetFirstText(e, 'FNAM - Flags', 'Flags', '', '', '0'), '0'));
end;

procedure AddAmmoFields(sl: TStringList; e: IInterface);
begin
  AddCommonItemFields(sl, e);
  AddFormRefKV(sl, 'projectile', LinkedByPath(e, 'DATA - Data\Projectile'));
  AddKV(sl, '  ', 'damage', SafeFloat(GetFirstText(e, 'DATA - Data\Damage', 'DATA\Damage', 'Damage', '', '0.0'), '0.0'));
  AddKV(sl, '  ', 'ammoFlags', AmmoFlagsFromRecord(e));
end;

procedure AddWeaponFields(sl: TStringList; e: IInterface);
begin
  AddCommonItemFields(sl, e);
  AddKV(sl, '  ', 'damage', SafeFloat(GetFirstText(e, 'DATA - Game Data\Damage', 'DNAM - Data\Damage', 'Damage', '', '0.0'), '0.0'));
  AddKV(sl, '  ', 'enchantmentAmount', SafeInt(GetText(e, 'EAMT - Enchantment Amount'), '0'));
  AddFormRefKV(sl, 'enchantment', LinkedByPath(e, 'EITM - Object Effect'));
  AddFormRefKV(sl, 'equipSlot', LinkedByPath(e, 'ETYP - Equipment Type'));
  AddFormRefKV(sl, 'templateWeapon', LinkedByPath(e, 'CNAM - Template'));
  AddFormRefKV(sl, 'critEffect', LinkedByPath(e, 'CRDT - Critical Data\Effect'));
  AddFormRefKV(sl, 'blockBashImpactDataSet', LinkedByPath(e, 'BIDS - Block Bash Impact Data Set'));
  AddFormRefKV(sl, 'altBlockMaterialType', LinkedByPath(e, 'BAMT - Alternate Block Material'));
  AddFormRefKV(sl, 'impactDataSet', LinkedByPath(e, 'INAM - Impact Data Set'));
  AddFormRefKV(sl, 'firstPersonModelObject', LinkedByPath(e, 'WNAM - 1st Person Model Object'));
  AddFormRefKV(sl, 'attackSound', LinkedByPath(e, 'SNAM - Attack Sound'));
  AddFormRefKV(sl, 'attackSound2D', LinkedByPath(e, 'XNAM - Attack Sound 2D'));
  AddFormRefKV(sl, 'attackLoopSound', LinkedByPath(e, 'NAM7 - Attack Loop Sound'));
  AddFormRefKV(sl, 'attackFailSound', LinkedByPath(e, 'TNAM - Attack Fail Sound'));
  AddFormRefKV(sl, 'idleSound', LinkedByPath(e, 'NAM6 - Idle Sound'));
  AddFormRefKV(sl, 'equipSound', LinkedByPath(e, 'NAM9 - Equip Sound'));
  AddFormRefKV(sl, 'unequipSound', LinkedByPath(e, 'NAM8 - Unequip Sound'));
  AddKV(sl, '  ', 'weaponType', WeaponTypeFromRecord(e));
  AddKV(sl, '  ', 'weaponFlags', '0');
  AddKV(sl, '  ', 'weaponFlags2', '0');
  AddKV(sl, '  ', 'weaponSkill', SafeInt(GetFirstText(e, 'DNAM - Data\Skill', 'DNAM\Skill', 'Skill', '', '6'), '6'));
  AddKV(sl, '  ', 'weaponResist', SafeInt(GetFirstText(e, 'DNAM - Data\Resist', 'DNAM\Resist', 'Resist', '', '24'), '24'));
  AddKV(sl, '  ', 'weaponCritFlags', '0');
  AddKV(sl, '  ', 'weaponCritDamage', SafeInt(GetFirstText(e, 'CRDT - Critical Data\Damage', 'CRDT\Damage', 'Critical Damage', '', '0'), '0'));
  AddKV(sl, '  ', 'weaponSpeed', SafeFloat(GetFirstText(e, 'DNAM - Data\Speed', 'DNAM\Speed', 'Speed', '', '1.0'), '1.0'));
  AddKV(sl, '  ', 'weaponReach', SafeFloat(GetFirstText(e, 'DNAM - Data\Reach', 'DNAM\Reach', 'Reach', '', '1.0'), '1.0'));
  AddKV(sl, '  ', 'weaponMinRange', SafeFloat(GetFirstText(e, 'DNAM - Data\Min Range', 'DNAM\Min Range', 'Min Range', '', '0.0'), '0.0'));
  AddKV(sl, '  ', 'weaponMaxRange', SafeFloat(GetFirstText(e, 'DNAM - Data\Max Range', 'DNAM\Max Range', 'Max Range', '', '0.0'), '0.0'));
  AddKV(sl, '  ', 'weaponStagger', SafeFloat(GetFirstText(e, 'DNAM - Data\Stagger', 'DNAM\Stagger', 'Stagger', '', '0.0'), '0.0'));
  AddKV(sl, '  ', 'weaponCritMult', SafeFloat(GetFirstText(e, 'CRDT - Critical Data\% Mult', 'CRDT\% Mult', 'Critical Mult', '', '1.0'), '1.0'));
end;

procedure AddAlchemyFields(sl: TStringList; e: IInterface);
begin
  AddCommonItemFields(sl, e);
  AddFormRefKV(sl, 'equipSlot', LinkedByPath(e, 'ETYP - Equipment Type'));
  AddFormRefKV(sl, 'addictionItem', LinkedByPath(e, 'ENIT - Effect Data\Addiction'));
  AddFormRefKV(sl, 'consumptionSound', LinkedByPath(e, 'YNAM - Pick Up Sound'));
  AddKV(sl, '  ', 'alchemyFlags', AlchemyFlagsFromRecord(e));
  AddKV(sl, '  ', 'alchemyCostOverride', SafeInt(GetFirstText(e, 'ENIT - Effect Data\Value', 'ENIT\Value', 'Value', '', '0'), '0'));
  AddKV(sl, '  ', 'addictionChance', SafeFloat(GetFirstText(e, 'ENIT - Effect Data\Addiction Chance', 'ENIT\Addiction Chance', 'Addiction Chance', '', '0.0'), '0.0'));
  AddSpellMagicEffects(sl, e);
end;

procedure AddIngredientFields(sl: TStringList; e: IInterface);
begin
  AddCommonItemFields(sl, e);
  AddFormRefKV(sl, 'equipSlot', LinkedByPath(e, 'ETYP - Equipment Type'));
  AddKV(sl, '  ', 'ingredientFlags', IngredientFlagsFromRecord(e));
  AddKV(sl, '  ', 'ingredientCostOverride', SafeInt(GetFirstText(e, 'ENIT - Effect Data\Value', 'ENIT\Value', 'Value', '', '0'), '0'));
  AddKV(sl, '  ', 'knownEffectFlags', SafeInt(GetFirstText(e, 'ENIT - Effect Data\Known Effect Flags', 'ENIT\Known Effect Flags', 'Known Effect Flags', '', '0'), '0'));
  AddKV(sl, '  ', 'playerUses', SafeInt(GetFirstText(e, 'ENIT - Effect Data\Player Uses', 'ENIT\Player Uses', 'Player Uses', '', '0'), '0'));
  AddSpellMagicEffects(sl, e);
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

procedure AddDecalFields(sl: TStringList; e: IInterface; const structName: string);
begin
  AddKV(sl, '  ', 'decalMinWidth', JsonFloat(StructFieldText(e, structName, 'Min Width', '0.0'), '0.0'));
  AddKV(sl, '  ', 'decalMaxWidth', JsonFloat(StructFieldText(e, structName, 'Max Width', '0.0'), '0.0'));
  AddKV(sl, '  ', 'decalMinHeight', JsonFloat(StructFieldText(e, structName, 'Min Height', '0.0'), '0.0'));
  AddKV(sl, '  ', 'decalMaxHeight', JsonFloat(StructFieldText(e, structName, 'Max Height', '0.0'), '0.0'));
  AddKV(sl, '  ', 'decalDepth', JsonFloat(StructFieldText(e, structName, 'Depth', '0.0'), '0.0'));
  AddKV(sl, '  ', 'decalShininess', JsonFloat(StructFieldText(e, structName, 'Shininess', '0.0'), '0.0'));
  AddKV(sl, '  ', 'decalParallaxScale', JsonFloat(StructFieldText(e, structName, 'Parallax Scale', '0.0'), '0.0'));
  AddKV(sl, '  ', 'decalParallaxPasses', JsonInt(StructFieldText(e, structName, 'Parallax Passes', '0'), '0'));
  AddKV(sl, '  ', 'decalFlags', ElementIntegerText(StructFieldElement(e, structName, 'Flags'), '0'));
  AddKV(sl, '  ', 'decalRed', JsonInt(StructFieldText(e, structName, 'Color\Red', '255'), '255'));
  AddKV(sl, '  ', 'decalGreen', JsonInt(StructFieldText(e, structName, 'Color\Green', '255'), '255'));
  AddKV(sl, '  ', 'decalBlue', JsonInt(StructFieldText(e, structName, 'Color\Blue', '255'), '255'));
  AddKV(sl, '  ', 'decalAlpha', JsonInt(StructFieldText(e, structName, 'Color\Alpha', '255'), '255'));
end;

procedure AddTextureSetFields(sl: TStringList; e: IInterface);
var
  hasDecal: Boolean;
begin
  sl.Add('  "textureSetPaths": [');
  sl.Add('    ' + JStr(GetText(e, 'TX00 - Diffuse')) + ',');
  sl.Add('    ' + JStr(GetText(e, 'TX01 - Normal/Gloss')) + ',');
  sl.Add('    ' + JStr(GetText(e, 'TX02 - Environment Mask/Subsurface Tint')) + ',');
  sl.Add('    ' + JStr(GetText(e, 'TX03 - Glow/Detail Map')) + ',');
  sl.Add('    ' + JStr(GetText(e, 'TX04 - Height')) + ',');
  sl.Add('    ' + JStr(GetText(e, 'TX05 - Environment')) + ',');
  sl.Add('    ' + JStr(GetText(e, 'TX06 - Multilayer')) + ',');
  sl.Add('    ' + JStr(GetText(e, 'TX07 - Backlight Mask/Specular')));
  sl.Add('  ],');
  AddKV(sl, '  ', 'textureSetFlags', ElementIntegerText(FirstElementByPath(e, 'DNAM - Flags', 'DNAM', 'Flags', ''), '0'));
  hasDecal := Assigned(FirstElementByPath(e, 'DODT - Decal Data', 'DODT', 'Decal Data', ''));
  AddKV(sl, '  ', 'textureSetHasDecal', BoolJson(hasDecal));
  if hasDecal then AddDecalFields(sl, e, 'DODT - Decal Data');
end;

procedure AddHazardFields(sl: TStringList; e: IInterface);
begin
  AddStringKV(sl, '  ', 'fullName', GetText(e, 'FULL - Name'));
  AddStringKV(sl, '  ', 'modelPath', GetText(e, 'Model\MODL - Model FileName'));
  AddKV(sl, '  ', 'hazardLimit', JsonInt(StructFieldText(e, 'DATA', 'Limit', '0'), '0'));
  AddKV(sl, '  ', 'hazardRadius', JsonFloat(StructFieldText(e, 'DATA', 'Radius', '0.0'), '0.0'));
  AddKV(sl, '  ', 'hazardLifetime', JsonFloat(StructFieldText(e, 'DATA', 'Lifetime', '0.0'), '0.0'));
  AddKV(sl, '  ', 'hazardImageSpaceRadius', JsonFloat(StructFieldText(e, 'DATA', 'Image Space Radius', '0.0'), '0.0'));
  AddKV(sl, '  ', 'hazardTargetInterval', JsonFloat(StructFieldText(e, 'DATA', 'Target Scan Interval', '0.0'), '0.0'));
  AddKV(sl, '  ', 'hazardFlags', ElementIntegerText(StructFieldElement(e, 'DATA', 'Flags'), '0'));
  AddFormRefKV(sl, 'hazardSpell', StructFieldLinked(e, 'DATA', 'Spell'));
  AddFormRefKV(sl, 'hazardLight', StructFieldLinked(e, 'DATA', 'Light'));
  AddFormRefKV(sl, 'hazardImpactDataSet', StructFieldLinked(e, 'DATA', 'Impact Data Set'));
  AddFormRefKV(sl, 'hazardSound', StructFieldLinked(e, 'DATA', 'Sound'));
  AddFormRefKV(sl, 'hazardImageSpaceModifier', LinkedByPath(e, 'MNAM - Image Space Modifier'));
end;

procedure AddImpactDataFields(sl: TStringList; e: IInterface);
begin
  AddStringKV(sl, '  ', 'modelPath', GetText(e, 'Model\MODL - Model FileName'));
  AddKV(sl, '  ', 'impactEffectDuration', JsonFloat(StructFieldText(e, 'DATA', 'Effect Duration', '0.0'), '0.0'));
  AddKV(sl, '  ', 'impactOrientation', ElementIntegerText(StructFieldElement(e, 'DATA', 'Effect Orientation'), '0'));
  AddKV(sl, '  ', 'impactAngleThreshold', JsonFloat(StructFieldText(e, 'DATA', 'Angle Threshold', '0.0'), '0.0'));
  AddKV(sl, '  ', 'impactPlacementRadius', JsonFloat(StructFieldText(e, 'DATA', 'Placement Radius', '0.0'), '0.0'));
  AddKV(sl, '  ', 'impactSoundLevel', ElementIntegerText(StructFieldElement(e, 'DATA', 'Sound Level'), '1'));
  AddKV(sl, '  ', 'impactFlags', ElementIntegerText(StructFieldElement(e, 'DATA', 'Flags'), '0'));
  AddKV(sl, '  ', 'impactResultOverride', ElementIntegerText(StructFieldElement(e, 'DATA', 'Impact Result'), '0'));
  AddFormRefKV(sl, 'impactDecalTextureSet', LinkedByPath(e, 'DNAM - Decal'));
  AddFormRefKV(sl, 'impactDecalTextureSet2', LinkedByPath(e, 'ENAM - Decal 2'));
  AddFormRefKV(sl, 'impactSound1', LinkedByPath(e, 'SNAM - Sound 1'));
  AddFormRefKV(sl, 'impactSound2', LinkedByPath(e, 'NAM1 - Sound 2'));
  AddFormRefKV(sl, 'impactHazard', LinkedByPath(e, 'NAM2 - Hazard'));
  AddDecalFields(sl, e, 'DODT - Decal Data');
end;

procedure AddReferenceEffectFields(sl: TStringList; e: IInterface);
begin
  AddFormRefKV(sl, 'referenceEffectArtObject', StructFieldLinked(e, 'DATA', 'Art Object'));
  AddFormRefKV(sl, 'referenceEffectShader', StructFieldLinked(e, 'DATA', 'Effect Shader'));
  AddKV(sl, '  ', 'referenceEffectFlags', ElementIntegerText(StructFieldElement(e, 'DATA', 'Flags'), '0'));
end;

procedure AddDualCastDataFields(sl: TStringList; e: IInterface);
begin
  AddFormRefKV(sl, 'dualCastProjectile', StructFieldLinked(e, 'DATA', 'Projectile'));
  AddFormRefKV(sl, 'dualCastExplosion', StructFieldLinked(e, 'DATA', 'Explosion'));
  AddFormRefKV(sl, 'dualCastEffectShader', StructFieldLinked(e, 'DATA', 'Effect Shader'));
  AddFormRefKV(sl, 'dualCastHitEffectArt', StructFieldLinked(e, 'DATA', 'Hit Effect Art'));
  AddFormRefKV(sl, 'dualCastImpactDataSet', StructFieldLinked(e, 'DATA', 'Impact Data Set'));
  AddKV(sl, '  ', 'dualCastFlags', ElementIntegerText(StructFieldElement(e, 'DATA', 'Flags'), '0'));
end;

procedure AddStaticFields(sl: TStringList; e: IInterface);
begin
  AddStringKV(sl, '  ', 'modelPath', GetText(e, 'Model\MODL - Model FileName'));
  AddKV(sl, '  ', 'staticMaterialThresholdAngle', JsonFloat(StructFieldText(e, 'DNAM', 'Max Angle', '0.0'), '0.0'));
  AddFormRefKV(sl, 'staticMaterialObject', StructFieldLinked(e, 'DNAM', 'Material'));
  AddKV(sl, '  ', 'staticFlags', ElementIntegerText(StructFieldElement(e, 'DNAM', 'Flags'), '0'));
  AddKV(sl, '  ', 'recordFlags', ElementIntegerText(FirstElementByPath(e, 'Record Header\Record Flags', 'Record Header\Flags', 'Record Flags', ''), '0'));
end;

procedure AddMovableStaticFields(sl: TStringList; e: IInterface);
begin
  AddStaticFields(sl, e);
  AddStringKV(sl, '  ', 'fullName', GetText(e, 'FULL - Name'));
  AddFormRefKV(sl, 'movableStaticSoundLoop', LinkedByPath(e, 'SNAM - Looping Sound'));
  AddKV(sl, '  ', 'movableStaticFlags', ElementIntegerText(FirstElementByPath(e, 'DATA - Flags', 'DATA', 'Flags', ''), '0'));
end;

procedure AddDoorFields(sl: TStringList; e: IInterface);
begin
  AddStringKV(sl, '  ', 'fullName', GetText(e, 'FULL - Name'));
  AddStringKV(sl, '  ', 'modelPath', GetText(e, 'Model\MODL - Model FileName'));
  AddFormRefKV(sl, 'doorOpenSound', LinkedByPath(e, 'SNAM - Open Sound'));
  AddFormRefKV(sl, 'doorCloseSound', LinkedByPath(e, 'ANAM - Close Sound'));
  AddFormRefKV(sl, 'doorLoopSound', LinkedByPath(e, 'BNAM - Loop Sound'));
  AddKV(sl, '  ', 'doorFlags', ElementIntegerText(FirstElementByPath(e, 'FNAM - Flags', 'FNAM', 'Flags', ''), '0'));
  AddKV(sl, '  ', 'recordFlags', ElementIntegerText(FirstElementByPath(e, 'Record Header\Record Flags', 'Record Header\Flags', 'Record Flags', ''), '0'));
end;

procedure AddCombatStyleFields(sl: TStringList; e: IInterface);
begin
  sl.Add('  "combatGeneral": [' +
    JsonFloat(StructFieldText(e, 'CSGD', 'Offensive Mult', '0'), '0') + ', ' + JsonFloat(StructFieldText(e, 'CSGD', 'Defensive Mult', '0'), '0') + ', ' +
    JsonFloat(StructFieldText(e, 'CSGD', 'Group Offensive Mult', '0'), '0') + ', ' + JsonFloat(StructFieldText(e, 'CSGD', 'Melee Score Mult', '0'), '0') + ', ' +
    JsonFloat(StructFieldText(e, 'CSGD', 'Magic Score Mult', '0'), '0') + ', ' + JsonFloat(StructFieldText(e, 'CSGD', 'Ranged Score Mult', '0'), '0') + ', ' +
    JsonFloat(StructFieldText(e, 'CSGD', 'Shout Score Mult', '0'), '0') + ', ' + JsonFloat(StructFieldText(e, 'CSGD', 'Unarmed Score Mult', '0'), '0') + ', ' +
    JsonFloat(StructFieldText(e, 'CSGD', 'Staff Score Mult', '0'), '0') + ', ' + JsonFloat(StructFieldText(e, 'CSGD', 'Avoid Threat Chance', '0'), '0') + '],');
  sl.Add('  "combatMelee": [' + JsonFloat(StructFieldText(e, 'CSME', 'Attack Incapacitated Mult', '0'), '0') + ', ' +
    JsonFloat(StructFieldText(e, 'CSME', 'Power Attack Incapacitated Mult', '0'), '0') + ', ' + JsonFloat(StructFieldText(e, 'CSME', 'Power Attack Blocking Mult', '0'), '0') + ', ' +
    JsonFloat(StructFieldText(e, 'CSME', 'Bash Mult', '0'), '0') + ', ' + JsonFloat(StructFieldText(e, 'CSME', 'Bash Recoil Mult', '0'), '0') + ', ' +
    JsonFloat(StructFieldText(e, 'CSME', 'Bash Attack Mult', '0'), '0') + ', ' + JsonFloat(StructFieldText(e, 'CSME', 'Bash Power Attack Mult', '0'), '0') + ', ' +
    JsonFloat(StructFieldText(e, 'CSME', 'Special Attack Mult', '0'), '0') + '],');
  sl.Add('  "combatCloseRange": [' + JsonFloat(StructFieldText(e, 'CSCR', 'Circle Mult', '0'), '0') + ', ' + JsonFloat(StructFieldText(e, 'CSCR', 'Fallback Mult', '0'), '0') + ', ' + JsonFloat(StructFieldText(e, 'CSCR', 'Flank Distance Mult', '0'), '0') + ', ' + JsonFloat(StructFieldText(e, 'CSCR', 'Stalk Time Mult', '0'), '0') + '],');
  AddKV(sl, '  ', 'combatLongRangeStrafe', JsonFloat(StructFieldText(e, 'CSLR', 'Strafe Mult', '0'), '0'));
  sl.Add('  "combatFlight": [' + JsonFloat(StructFieldText(e, 'CSFL', 'Hover Chance', '0'), '0') + ', ' + JsonFloat(StructFieldText(e, 'CSFL', 'Dive Bomb Chance', '0'), '0') + ', ' + JsonFloat(StructFieldText(e, 'CSFL', 'Ground Attack Chance', '0'), '0') + ', ' + JsonFloat(StructFieldText(e, 'CSFL', 'Hover Time Mult', '0'), '0') + ', ' + JsonFloat(StructFieldText(e, 'CSFL', 'Ground Attack Time Mult', '0'), '0') + ', ' + JsonFloat(StructFieldText(e, 'CSFL', 'Perch Attack Chance', '0'), '0') + ', ' + JsonFloat(StructFieldText(e, 'CSFL', 'Perch Attack Time Mult', '0'), '0') + ', ' + JsonFloat(StructFieldText(e, 'CSFL', 'Flying Attack Chance', '0'), '0') + '],');
  AddKV(sl, '  ', 'combatStyleFlags', ElementIntegerText(FirstElementByPath(e, 'DATA - Flags', 'DATA', 'Flags', ''), '0'));
end;

procedure AddSoundCategoryFields(sl: TStringList; e: IInterface);
begin
  AddStringKV(sl, '  ', 'fullName', GetText(e, 'FULL - Name'));
  AddKV(sl, '  ', 'soundCategoryFlags', ElementIntegerText(FirstElementByPath(e, 'FNAM - Flags', 'FNAM', 'Flags', ''), '0'));
  AddFormRefKV(sl, 'soundCategoryParent', LinkedByPath(e, 'SNCT - Parent'));
  AddKV(sl, '  ', 'soundCategoryAttenuation', JsonInt(GetFirstText(e, 'PNAM - Static Attenuation', 'DATA\Attenuation', 'Attenuation', '', '0'), '0'));
  AddKV(sl, '  ', 'soundCategoryStaticMult', JsonFloat(GetFirstText(e, 'VNAM - Static Volume Multiplier', 'VNAM', 'Static Volume Multiplier', '', '1.0'), '1.0'));
  AddKV(sl, '  ', 'soundCategoryDefaultMenuValue', JsonFloat(GetFirstText(e, 'UNAM - Default Menu Value', 'UNAM', 'Default Menu Value', '', '1.0'), '1.0'));
  AddKV(sl, '  ', 'soundCategoryVolumeMult', JsonFloat(GetFirstText(e, 'DATA\Volume Multiplier', 'Volume Multiplier', '', '', '1.0'), '1.0'));
  AddKV(sl, '  ', 'soundCategoryFrequencyMult', JsonFloat(GetFirstText(e, 'DATA\Frequency Multiplier', 'Frequency Multiplier', '', '', '1.0'), '1.0'));
end;

procedure AddClassFields(sl: TStringList; e: IInterface);
begin
  AddStringKV(sl, '  ', 'fullName', GetText(e, 'FULL - Name'));
  AddStringKV(sl, '  ', 'description', GetText(e, 'DESC - Description'));
  AddStringKV(sl, '  ', 'classIconPath', GetText(e, 'ICON - Icon'));
  AddKV(sl, '  ', 'classTeachesSkill', ElementIntegerText(StructFieldElement(e, 'DATA', 'Teaches'), '0'));
  AddKV(sl, '  ', 'classMaximumTrainingLevel', JsonInt(StructFieldText(e, 'DATA', 'Maximum Training Level', '0'), '0'));
  sl.Add('  "classSkillWeights": [' + JsonInt(StructFieldText(e, 'DATA', 'One-Handed', '0'), '0') + ', ' + JsonInt(StructFieldText(e, 'DATA', 'Two-Handed', '0'), '0') + ', ' + JsonInt(StructFieldText(e, 'DATA', 'Archery', '0'), '0') + ', ' + JsonInt(StructFieldText(e, 'DATA', 'Block', '0'), '0') + ', ' + JsonInt(StructFieldText(e, 'DATA', 'Smithing', '0'), '0') + ', ' + JsonInt(StructFieldText(e, 'DATA', 'Heavy Armor', '0'), '0') + ', ' + JsonInt(StructFieldText(e, 'DATA', 'Light Armor', '0'), '0') + ', ' + JsonInt(StructFieldText(e, 'DATA', 'Pickpocket', '0'), '0') + ', ' + JsonInt(StructFieldText(e, 'DATA', 'Lockpicking', '0'), '0') + ', ' + JsonInt(StructFieldText(e, 'DATA', 'Sneak', '0'), '0') + ', ' + JsonInt(StructFieldText(e, 'DATA', 'Alchemy', '0'), '0') + ', ' + JsonInt(StructFieldText(e, 'DATA', 'Speech', '0'), '0') + ', ' + JsonInt(StructFieldText(e, 'DATA', 'Alteration', '0'), '0') + ', ' + JsonInt(StructFieldText(e, 'DATA', 'Conjuration', '0'), '0') + ', ' + JsonInt(StructFieldText(e, 'DATA', 'Destruction', '0'), '0') + ', ' + JsonInt(StructFieldText(e, 'DATA', 'Illusion', '0'), '0') + ', ' + JsonInt(StructFieldText(e, 'DATA', 'Restoration', '0'), '0') + ', ' + JsonInt(StructFieldText(e, 'DATA', 'Enchanting', '0'), '0') + '],');
  AddKV(sl, '  ', 'classBleedoutDefault', JsonFloat(StructFieldText(e, 'DATA', 'Bleedout Default', '0.0'), '0.0'));
  AddKV(sl, '  ', 'classVoicePoints', JsonInt(StructFieldText(e, 'DATA', 'Voice Points', '0'), '0'));
  sl.Add('  "classAttributeWeights": [' + JsonInt(StructFieldText(e, 'DATA', 'Health', '0'), '0') + ', ' + JsonInt(StructFieldText(e, 'DATA', 'Magicka', '0'), '0') + ', ' + JsonInt(StructFieldText(e, 'DATA', 'Stamina', '0'), '0') + '],');
end;

procedure AddProduceFields(sl: TStringList; e: IInterface);
begin
  AddFormRefKV(sl, 'produceItem', LinkedByPath(e, 'PFIG - Ingredient'));
  AddFormRefKV(sl, 'harvestSound', LinkedByPath(e, 'SNAM - Harvest Sound'));
  sl.Add('  "produceChance": [' + JsonInt(GetFirstText(e, 'PFPC - Ingredient Production\Spring', 'PFPC\Spring', 'Spring', '', '0'), '0') + ', ' + JsonInt(GetFirstText(e, 'PFPC - Ingredient Production\Summer', 'PFPC\Summer', 'Summer', '', '0'), '0') + ', ' + JsonInt(GetFirstText(e, 'PFPC - Ingredient Production\Fall', 'PFPC\Fall', 'Fall', '', '0'), '0') + ', ' + JsonInt(GetFirstText(e, 'PFPC - Ingredient Production\Winter', 'PFPC\Winter', 'Winter', '', '0'), '0') + '],');
end;

procedure AddFloraFields(sl: TStringList; e: IInterface);
begin
  AddStringKV(sl, '  ', 'fullName', GetText(e, 'FULL - Name'));
  AddStringKV(sl, '  ', 'modelPath', GetText(e, 'Model\MODL - Model FileName'));
  AddProduceFields(sl, e);
  AddFormRefArrayFromElement(sl, 'keywords', ElementByPath(e, 'KWDA - Keywords'));
  AddKV(sl, '  ', 'floraFlags', ElementIntegerText(FirstElementByPath(e, 'FNAM - Flags', 'FNAM', 'Flags', ''), '0'));
  AddFormRefKV(sl, 'floraSoundLoop', LinkedByPath(e, 'SNAM - Looping Sound'));
  AddFormRefKV(sl, 'floraSoundActivate', LinkedByPath(e, 'VNAM - Activation Sound'));
  AddFormRefKV(sl, 'floraWaterType', LinkedByPath(e, 'WNAM - Water Type'));
end;

procedure AddTreeFields(sl: TStringList; e: IInterface);
begin
  AddStringKV(sl, '  ', 'fullName', GetText(e, 'FULL - Name'));
  AddStringKV(sl, '  ', 'modelPath', GetText(e, 'Model\MODL - Model FileName'));
  AddProduceFields(sl, e);
  AddKV(sl, '  ', 'treeType', ElementIntegerText(FirstElementByPath(e, 'DNAM - Tree Type', 'DNAM', 'Tree Type', ''), '0'));
  AddKV(sl, '  ', 'recordFlags', ElementIntegerText(FirstElementByPath(e, 'Record Header\Record Flags', 'Record Header\Flags', 'Record Flags', ''), '0'));
  sl.Add('  "treeAnimation": [' + JsonFloat(StructFieldText(e, 'CNAM', 'Trunk Flexibility', '0'), '0') + ', ' + JsonFloat(StructFieldText(e, 'CNAM', 'Branch Flexibility', '0'), '0') + ', ' + JsonFloat(StructFieldText(e, 'CNAM', 'Trunk Amplitude', '0'), '0') + ', ' + JsonFloat(StructFieldText(e, 'CNAM', 'Front Amplitude', '0'), '0') + ', ' + JsonFloat(StructFieldText(e, 'CNAM', 'Back Amplitude', '0'), '0') + ', ' + JsonFloat(StructFieldText(e, 'CNAM', 'Side Amplitude', '0'), '0') + ', ' + JsonFloat(StructFieldText(e, 'CNAM', 'Front Frequency', '0'), '0') + ', ' + JsonFloat(StructFieldText(e, 'CNAM', 'Back Frequency', '0'), '0') + ', ' + JsonFloat(StructFieldText(e, 'CNAM', 'Side Frequency', '0'), '0') + ', ' + JsonFloat(StructFieldText(e, 'CNAM', 'Leaf Flexibility', '0'), '0') + ', ' + JsonFloat(StructFieldText(e, 'CNAM', 'Leaf Amplitude', '0'), '0') + ', ' + JsonFloat(StructFieldText(e, 'CNAM', 'Leaf Frequency', '0'), '0') + '],');
end;

procedure WriteConstructibleComponents(sl: TStringList; node: IInterface; var written: Integer);
var
  child, itemForm, countElement: IInterface;
  i: Integer;
  count: string;
begin
  if not Assigned(node) then Exit;
  if Pos('cnto', LowerCase(Name(node))) > 0 then begin
    countElement := FindDescendantByExactName(node, 'Count');
    itemForm := FirstLinkedInElement(node);
    if Assigned(countElement) and Assigned(itemForm) then begin
      count := GetEditValue(countElement);
      sl.Add('    {');
      AddKV(sl, '      ', 'item', FormRefJson(itemForm));
      AddKV(sl, '      ', 'count', JsonInt(count, '1'));
      RemoveTrailingComma(sl);
      sl.Add('    },');
      Inc(written);
      Exit;
    end;
  end;
  for i := 0 to ElementCount(node) - 1 do begin
    child := ElementByIndex(node, i);
    WriteConstructibleComponents(sl, child, written);
  end;
end;

procedure AddConstructibleObjectFields(sl: TStringList; e: IInterface);
var
  written: Integer;
  created, bench: IInterface;
begin
  created := FirstLinkedInElement(FirstElementByPath(e, 'CNAM - Created Object', 'CNAM', 'Created Object', ''));
  bench := FirstLinkedInElement(FirstElementByPath(e, 'BNAM - Workbench Keyword', 'BNAM', 'Workbench Keyword', ''));
  AddFormRefKV(sl, 'createdItem', created);
  AddFormRefKV(sl, 'benchKeyword', bench);
  AddKV(sl, '  ', 'numConstructed', JsonInt(GetFirstText(e, 'NAM1 - Created Object Count', 'NAM1', 'Created Object Count', '', '1'), '1'));
  sl.Add('  "requiredItems": [');
  written := 0;
  WriteConstructibleComponents(sl, e, written);
  RemoveTrailingComma(sl);
  sl.Add('  ],');
  AddPerkConditionsArray(sl, 'conditions', e, '  ');
end;

procedure WriteContainerItems(sl: TStringList; node: IInterface; var written: Integer);
var
  i: Integer;
  child, cnto, coed, itemForm, countElement, ownerForm, globalForm: IInterface;
  count, requiredRank, healthMult: string;
begin
  if not Assigned(node) then Exit;
  cnto := nil;
  if Pos('item', LowerCase(Name(node))) > 0 then cnto := FindChildByNameContains(node, 'CNTO');
  if not Assigned(cnto) and (Pos('cnto', LowerCase(Name(node))) > 0) then cnto := node;
  if Assigned(cnto) then begin
    itemForm := FirstLinkedInElement(cnto);
    countElement := FindDescendantByExactName(cnto, 'Count');
    if Assigned(itemForm) and Assigned(countElement) then begin
      coed := FindChildByNameContains(node, 'COED');
      ownerForm := nil;
      globalForm := nil;
      requiredRank := '0';
      healthMult := '100.0';
      if Assigned(coed) then begin
        ownerForm := StructFieldLinked(node, 'COED', 'Owner');
        globalForm := StructFieldLinked(node, 'COED', 'Global');
        requiredRank := StructFieldText(node, 'COED', 'Required Rank', '0');
        healthMult := StructFieldText(node, 'COED', 'Item Condition', '100.0');
      end;
      count := GetEditValue(countElement);
      sl.Add('    {');
      AddKV(sl, '      ', 'item', FormRefJson(itemForm));
      AddKV(sl, '      ', 'count', JsonInt(count, '1'));
      if Assigned(ownerForm) then AddKV(sl, '      ', 'owner', FormRefJson(ownerForm));
      if Assigned(globalForm) then AddKV(sl, '      ', 'conditionGlobal', FormRefJson(globalForm));
      AddKV(sl, '      ', 'requiredRank', JsonInt(requiredRank, '0'));
      AddKV(sl, '      ', 'healthMult', JsonFloat(healthMult, '100.0'));
      RemoveTrailingComma(sl);
      sl.Add('    },');
      Inc(written);
      Exit;
    end;
  end;
  for i := 0 to ElementCount(node) - 1 do begin
    child := ElementByIndex(node, i);
    WriteContainerItems(sl, child, written);
  end;
end;

procedure AddContainerFields(sl: TStringList; e: IInterface);
var
  written: Integer;
  openSound, closeSound: IInterface;
begin
  AddStringKV(sl, '  ', 'fullName', GetText(e, 'FULL - Name'));
  AddStringKV(sl, '  ', 'modelPath', GetText(e, 'Model\MODL - Model FileName'));
  AddKV(sl, '  ', 'itemWeight', JsonFloat(StructFieldText(e, 'DATA', 'Weight', '0.0'), '0.0'));
  AddKV(sl, '  ', 'containerFlags', ElementIntegerText(StructFieldElement(e, 'DATA', 'Flags'), '0'));
  AddKV(sl, '  ', 'containerAllowStolenItems', 'false');
  openSound := FirstLinkedInElement(FirstElementByPath(e, 'SNAM - Open Sound', 'SNAM', 'Open Sound', ''));
  closeSound := FirstLinkedInElement(FirstElementByPath(e, 'QNAM - Close Sound', 'QNAM', 'Close Sound', ''));
  AddFormRefKV(sl, 'containerOpenSound', openSound);
  AddFormRefKV(sl, 'containerCloseSound', closeSound);
  AddKV(sl, '  ', 'recordFlags', ElementIntegerText(FirstElementByPath(e, 'Record Header\Record Flags', 'Record Header\Flags', 'Record Flags', ''), '0'));
  sl.Add('  "containerItems": [');
  written := 0;
  WriteContainerItems(sl, e, written);
  RemoveTrailingComma(sl);
  sl.Add('  ],');
end;

procedure AddImpactDataSetFields(sl: TStringList; e: IInterface);
var
  arr, entry, materialRef, impactRef: IInterface;
  i: Integer;
begin
  sl.Add('  "impactDataSetEntries": [');
  arr := FirstElementByPath(e, 'PNAM - Impact Data', 'PNAM', 'Impact Data', '');
  if Assigned(arr) then
    for i := 0 to ElementCount(arr) - 1 do begin
      entry := ElementByIndex(arr, i);
      materialRef := StructFieldLinked(entry, '', 'Material Type');
      impactRef := StructFieldLinked(entry, '', 'Impact Data');
      if not Assigned(materialRef) then materialRef := FirstLinkedInElement(ElementByIndex(entry, 0));
      if not Assigned(impactRef) and (ElementCount(entry) > 1) then impactRef := FirstLinkedInElement(ElementByIndex(entry, 1));
      if Assigned(materialRef) and Assigned(impactRef) then
        sl.Add('    { "key": ' + FormRefJson(materialRef) + ', "value": ' + FormRefJson(impactRef) + ' },');
    end;
  RemoveTrailingComma(sl);
  sl.Add('  ],');
end;

procedure AddCollisionLayerFields(sl: TStringList; e: IInterface);
begin
  AddKV(sl, '  ', 'collisionLayerIndex', SafeInt(FirstText(e, 'BNAM - Index', 'BNAM', ''), '0'));
  AddKV(sl, '  ', 'collisionLayerColor', IntToStr(GetElementNativeValues(e, 'FNAM')));
  AddKV(sl, '  ', 'collisionLayerFlags', IntToStr(GetElementNativeValues(e, 'GNAM')));
  AddStringKV(sl, '  ', 'collisionLayerName', FirstText(e, 'MNAM - Name', 'MNAM', ''));
  AddFormRefArrayFromPaths(sl, 'collisionLayers', e, 'CNAM - Collides With', 'CNAM', 'Collides With', '');
end;

procedure AddFootstepFields(sl: TStringList; e: IInterface);
begin
  AddStringKV(sl, '  ', 'footstepTag', FirstText(e, 'ANAM - Tag', 'ANAM', ''));
  AddFormRefKV(sl, 'footstepImpactDataSet', LinkedByPath(e, 'DATA - Impact Data Set'));
end;

procedure AddFootstepSetFields(sl: TStringList; e: IInterface);
begin
  AddFormRefArrayFromPaths(sl, 'footstepWalk', e, 'DATA - Footstep Sets\Walk', 'DATA\Walk', 'Walk', '');
  AddFormRefArrayFromPaths(sl, 'footstepRun', e, 'DATA - Footstep Sets\Run', 'DATA\Run', 'Run', '');
  AddFormRefArrayFromPaths(sl, 'footstepSneak', e, 'DATA - Footstep Sets\Sneak', 'DATA\Sneak', 'Sneak', '');
  AddFormRefArrayFromPaths(sl, 'footstepBleedout', e, 'DATA - Footstep Sets\Bleedout', 'DATA\Bleedout', 'Bleedout', '');
  AddFormRefArrayFromPaths(sl, 'footstepSwim', e, 'DATA - Footstep Sets\Swim', 'DATA\Swim', 'Swim', '');
end;

procedure AddReverbFields(sl: TStringList; e: IInterface);
begin
  AddKV(sl, '  ', 'reverbDecayTime', SafeInt(StructFieldText(e, 'DATA', 'Decay Time', '1000'), '1000'));
  AddKV(sl, '  ', 'reverbHFReference', SafeInt(StructFieldText(e, 'DATA', 'HF Reference', '5000'), '5000'));
  sl.Add('  "reverbValues": [' +
    SafeInt(StructFieldText(e, 'DATA', 'Room Filter', '0'), '0') + ', ' +
    SafeInt(StructFieldText(e, 'DATA', 'Room HF Filter', '0'), '0') + ', ' +
    SafeInt(StructFieldText(e, 'DATA', 'Reflections', '0'), '0') + ', ' +
    SafeInt(StructFieldText(e, 'DATA', 'Reverb', '0'), '0') + ', ' +
    SafeInt(StructFieldText(e, 'DATA', 'Decay HF Ratio', '0'), '0') + ', ' +
    SafeInt(StructFieldText(e, 'DATA', 'Reflection Delay', '0'), '0') + ', ' +
    SafeInt(StructFieldText(e, 'DATA', 'Reverb Delay', '0'), '0') + ', ' +
    SafeInt(StructFieldText(e, 'DATA', 'Diffusion', '0'), '0') + ', ' +
    SafeInt(StructFieldText(e, 'DATA', 'Density', '0'), '0') + '],');
end;

procedure AddAcousticSpaceFields(sl: TStringList; e: IInterface);
begin
  AddFormRefKV(sl, 'acousticLoopingSound', LinkedByPath(e, 'SNAM - Looping Sound'));
  AddFormRefKV(sl, 'acousticSoundRegion', LinkedByPath(e, 'RDAT - Sound Region'));
  AddFormRefKV(sl, 'acousticReverb', LinkedByPath(e, 'BNAM - Reverb'));
end;

procedure AddApparatusFields(sl: TStringList; e: IInterface);
begin
  AddCommonItemFields(sl, e);
  AddFormRefArrayFromPaths(sl, 'keywords', e, 'KWDA - Keywords', 'KWDA', 'Keywords', '');
  AddKV(sl, '  ', 'apparatusQuality', SafeInt(FirstText(e, 'QUAL - Quality', 'QUAL', ''), '0'));
end;

procedure AddStaticCollectionFields(sl: TStringList; e: IInterface);
begin
  AddStringKV(sl, '  ', 'modelPath', GetText(e, 'Model\MODL - Model FileName'));
  AddKV(sl, '  ', 'recordFlags', IntToStr(GetElementNativeValues(e, 'Record Header\Record Flags')));
end;

procedure AddGrassFields(sl: TStringList; e: IInterface);
begin
  AddStringKV(sl, '  ', 'modelPath', GetText(e, 'Model\MODL - Model FileName'));
  AddKV(sl, '  ', 'grassDensity', SafeInt(StructFieldText(e, 'DATA', 'Density', '50'), '50'));
  AddKV(sl, '  ', 'grassMinSlope', SafeInt(StructFieldText(e, 'DATA', 'Min Slope', '0'), '0'));
  AddKV(sl, '  ', 'grassMaxSlope', SafeInt(StructFieldText(e, 'DATA', 'Max Slope', '90'), '90'));
  AddKV(sl, '  ', 'grassDistanceFromWater', SafeInt(StructFieldText(e, 'DATA', 'Distance from Water', '0'), '0'));
  AddKV(sl, '  ', 'grassWaterState', SafeInt(StructFieldText(e, 'DATA', 'Water', '0'), '0'));
  AddKV(sl, '  ', 'grassPositionRange', SafeFloat(StructFieldText(e, 'DATA', 'Position Range', '0'), '0.0'));
  AddKV(sl, '  ', 'grassHeightRange', SafeFloat(StructFieldText(e, 'DATA', 'Height Range', '0'), '0.0'));
  AddKV(sl, '  ', 'grassColorRange', SafeFloat(StructFieldText(e, 'DATA', 'Color Range', '0'), '0.0'));
  AddKV(sl, '  ', 'grassWavePeriod', SafeFloat(StructFieldText(e, 'DATA', 'Wave Period', '1'), '1.0'));
  AddKV(sl, '  ', 'grassFlags', IntToStr(GetElementNativeValues(e, 'DATA\Flags')));
end;

procedure AddIdleMarkerFields(sl: TStringList; e: IInterface);
begin
  AddStringKV(sl, '  ', 'modelPath', GetText(e, 'Model\MODL - Model FileName'));
  AddKV(sl, '  ', 'idleFlags', IntToStr(GetElementNativeValues(e, 'IDLF - Flags')));
  AddKV(sl, '  ', 'idleTimer', SafeFloat(FirstText(e, 'IDLT - Idle Timer Setting', 'IDLT', ''), '0.0'));
  AddFormRefArrayFromPaths(sl, 'idleAnimations', e, 'IDLA - Animations', 'IDLA', 'Animations', '');
  AddKV(sl, '  ', 'recordFlags', IntToStr(GetElementNativeValues(e, 'Record Header\Record Flags')));
end;

procedure AddEncounterZoneFields(sl: TStringList; e: IInterface);
begin
  AddFormRefKV(sl, 'encounterOwner', StructFieldLinked(e, 'DATA', 'Owner'));
  AddFormRefKV(sl, 'encounterLocation', StructFieldLinked(e, 'DATA', 'Location'));
  AddKV(sl, '  ', 'encounterOwnerRank', SafeInt(StructFieldText(e, 'DATA', 'Rank', '0'), '0'));
  AddKV(sl, '  ', 'encounterMinLevel', SafeInt(StructFieldText(e, 'DATA', 'Minimum Level', '0'), '0'));
  AddKV(sl, '  ', 'encounterMaxLevel', SafeInt(StructFieldText(e, 'DATA', 'Maximum Level', '0'), '0'));
  AddKV(sl, '  ', 'encounterFlags', IntToStr(GetElementNativeValues(e, 'DATA\Flags')));
end;

procedure AddRelationshipFields(sl: TStringList; e: IInterface);
begin
  AddFormRefKV(sl, 'relationshipNpc1', StructFieldLinked(e, 'DATA', 'Parent'));
  AddFormRefKV(sl, 'relationshipNpc2', StructFieldLinked(e, 'DATA', 'Child'));
  AddFormRefKV(sl, 'relationshipAssociation', StructFieldLinked(e, 'DATA', 'Association Type'));
  AddKV(sl, '  ', 'relationshipLevel', SafeInt(StructFieldText(e, 'DATA', 'Relationship Level', '4'), '4'));
  AddKV(sl, '  ', 'relationshipFlags', IntToStr(GetElementNativeValues(e, 'DATA\Flags')));
end;

procedure AddAssociationTypeFields(sl: TStringList; e: IInterface);
begin
  sl.Add('  "associationLabels": [' +
    JStr(FirstText(e, 'MPRT - Male Parent Title', 'MPRT', '')) + ', ' + JStr(FirstText(e, 'FPRT - Female Parent Title', 'FPRT', '')) + ', ' +
    JStr(FirstText(e, 'MCHT - Male Child Title', 'MCHT', '')) + ', ' + JStr(FirstText(e, 'FCHT - Female Child Title', 'FCHT', '')) + '],');
  AddKV(sl, '  ', 'associationFlags', IntToStr(GetElementNativeValues(e, 'DATA - Flags')));
end;

procedure AddMovementTypeFields(sl: TStringList; e: IInterface);
begin
  AddStringKV(sl, '  ', 'movementName', FirstText(e, 'MNAM - Name', 'MNAM', ''));
  sl.Add('  "movementSpeeds": [' +
    SafeFloat(StructFieldText(e, 'SPED', 'Left - Walk', '0'), '0.0') + ', ' + SafeFloat(StructFieldText(e, 'SPED', 'Left - Run', '0'), '0.0') + ', ' +
    SafeFloat(StructFieldText(e, 'SPED', 'Right - Walk', '0'), '0.0') + ', ' + SafeFloat(StructFieldText(e, 'SPED', 'Right - Run', '0'), '0.0') + ', ' +
    SafeFloat(StructFieldText(e, 'SPED', 'Forward - Walk', '0'), '0.0') + ', ' + SafeFloat(StructFieldText(e, 'SPED', 'Forward - Run', '0'), '0.0') + ', ' +
    SafeFloat(StructFieldText(e, 'SPED', 'Back - Walk', '0'), '0.0') + ', ' + SafeFloat(StructFieldText(e, 'SPED', 'Back - Run', '0'), '0.0') + ', ' +
    SafeFloat(StructFieldText(e, 'SPED', 'Rotate - Walk', '0'), '0.0') + ', ' + SafeFloat(StructFieldText(e, 'SPED', 'Rotate - Run', '0'), '0.0') + '],');
  AddKV(sl, '  ', 'movementRotateWhileMoving', SafeFloat(StructFieldText(e, 'SPED', 'Rotate while Moving Run', '0'), '0.0'));
  AddKV(sl, '  ', 'movementDirectional', SafeFloat(StructFieldText(e, 'INAM', 'Directional', '0'), '0.0'));
  AddKV(sl, '  ', 'movementSpeed', SafeFloat(StructFieldText(e, 'INAM', 'Movement Speed', '0'), '0.0'));
  AddKV(sl, '  ', 'movementRotationSpeed', SafeFloat(StructFieldText(e, 'INAM', 'Rotation Speed', '0'), '0.0'));
end;

procedure AddWordOfPowerFields(sl: TStringList; e: IInterface);
begin
  AddStringKV(sl, '  ', 'fullName', GetText(e, 'FULL - Name'));
  AddStringKV(sl, '  ', 'wordTranslation', FirstText(e, 'TNAM - Translation', 'TNAM', ''));
end;

procedure AddWaterFields(sl: TStringList; e: IInterface);
begin
  AddStringKV(sl, '  ', 'fullName', GetText(e, 'FULL - Name'));
  sl.Add('  "waterNoiseTextures": [' + JStr(GetText(e, 'NAM2 - Noise Texture 1')) + ', ' + JStr(GetText(e, 'NAM3 - Noise Texture 2')) + ', ' + JStr(GetText(e, 'NAM4 - Noise Texture 3')) + ', ' + JStr(GetText(e, 'NAM5 - Noise Texture 4')) + '],');
  AddKV(sl, '  ', 'waterAlpha', SafeInt(FirstText(e, 'ANAM - Opacity', 'ANAM', ''), '255'));
  AddKV(sl, '  ', 'waterFlags', IntToStr(GetElementNativeValues(e, 'FNAM - Flags')));
  AddFormRefKV(sl, 'waterMaterial', LinkedByPath(e, 'TNAM - Material'));
  AddFormRefKV(sl, 'waterSound', LinkedByPath(e, 'SNAM - Sound'));
  AddFormRefKV(sl, 'waterContactSpell', LinkedByPath(e, 'XNAM - Contact Spell'));
  AddFormRefKV(sl, 'waterImageSpace', LinkedByPath(e, 'INAM - Image Space'));
  sl.Add('  "waterLinearVelocity": [' + SafeFloat(StructFieldText(e, 'NAM0', 'X', '0'), '0.0') + ', ' + SafeFloat(StructFieldText(e, 'NAM0', 'Y', '0'), '0.0') + ', ' + SafeFloat(StructFieldText(e, 'NAM0', 'Z', '0'), '0.0') + '],');
  sl.Add('  "waterAngularVelocity": [' + SafeFloat(StructFieldText(e, 'NAM1', 'X', '0'), '0.0') + ', ' + SafeFloat(StructFieldText(e, 'NAM1', 'Y', '0'), '0.0') + ', ' + SafeFloat(StructFieldText(e, 'NAM1', 'Z', '0'), '0.0') + '],');
end;

procedure AddImageSpaceFields(sl: TStringList; e: IInterface);
begin
  sl.Add('  "imageSpaceHDR": [' + SafeFloat(StructFieldText(e, 'HNAM', 'Eye Adapt Speed', '0'), '0.0') + ', ' + SafeFloat(StructFieldText(e, 'HNAM', 'Bloom Blur Radius', '0'), '0.0') + ', ' + SafeFloat(StructFieldText(e, 'HNAM', 'Bloom Threshold', '0'), '0.0') + ', ' + SafeFloat(StructFieldText(e, 'HNAM', 'Bloom Scale', '0'), '0.0') + ', ' + SafeFloat(StructFieldText(e, 'HNAM', 'Receive Bloom Threshold', '0'), '0.0') + ', ' + SafeFloat(StructFieldText(e, 'HNAM', 'White', '0'), '0.0') + ', ' + SafeFloat(StructFieldText(e, 'HNAM', 'Sunlight Scale', '0'), '0.0') + ', ' + SafeFloat(StructFieldText(e, 'HNAM', 'Sky Scale', '0'), '0.0') + ', ' + SafeFloat(StructFieldText(e, 'HNAM', 'Eye Adapt Strength', '0'), '0.0') + '],');
  sl.Add('  "imageSpaceCinematic": [' + SafeFloat(StructFieldText(e, 'CNAM', 'Saturation', '0'), '0.0') + ', ' + SafeFloat(StructFieldText(e, 'CNAM', 'Brightness', '0'), '0.0') + ', ' + SafeFloat(StructFieldText(e, 'CNAM', 'Contrast', '0'), '0.0') + '],');
  AddKV(sl, '  ', 'imageSpaceTintAmount', SafeFloat(StructFieldText(e, 'TNAM', 'Amount', '0'), '0.0'));
  sl.Add('  "imageSpaceTintColor": [' + SafeFloat(StructFieldText(e, 'TNAM', 'Red', '0'), '0.0') + ', ' + SafeFloat(StructFieldText(e, 'TNAM', 'Green', '0'), '0.0') + ', ' + SafeFloat(StructFieldText(e, 'TNAM', 'Blue', '0'), '0.0') + '],');
  sl.Add('  "imageSpaceDOF": [' + SafeFloat(StructFieldText(e, 'DNAM', 'Strength', '0'), '0.0') + ', ' + SafeFloat(StructFieldText(e, 'DNAM', 'Distance', '0'), '0.0') + ', ' + SafeFloat(StructFieldText(e, 'DNAM', 'Range', '0'), '0.0') + '],');
  AddKV(sl, '  ', 'imageSpaceDOFFlags', IntToStr(GetElementNativeValues(e, 'DNAM\Flags')));
  AddKV(sl, '  ', 'imageSpaceSkyBlur', SafeInt(StructFieldText(e, 'DNAM', 'Sky Blur Radius', '16384'), '16384'));
end;

procedure AddLightingTemplateFields(sl: TStringList; e: IInterface);
begin
  sl.Add('  "lightingColors": [' + IntToStr(GetElementNativeValues(e, 'DATA\Ambient Color')) + ', ' + IntToStr(GetElementNativeValues(e, 'DATA\Directional Color')) + ', ' + IntToStr(GetElementNativeValues(e, 'DATA\Fog Color Near')) + ', ' + IntToStr(GetElementNativeValues(e, 'DATA\Fog Color Far')) + ', 0, 0, 0],');
  sl.Add('  "lightingValues": [' + SafeFloat(StructFieldText(e, 'DATA', 'Fog Near', '0'), '0.0') + ', ' + SafeFloat(StructFieldText(e, 'DATA', 'Fog Far', '0'), '0.0') + ', ' + SafeFloat(StructFieldText(e, 'DATA', 'Directional Fade', '0'), '0.0') + ', ' + SafeFloat(StructFieldText(e, 'DATA', 'Clip Distance', '0'), '0.0') + ', ' + SafeFloat(StructFieldText(e, 'DATA', 'Fog Power', '0'), '0.0') + ', ' + SafeFloat(StructFieldText(e, 'DATA', 'Fog Clamp', '0'), '0.0') + ', ' + SafeFloat(StructFieldText(e, 'DATA', 'Light Fade Start', '0'), '0.0') + ', ' + SafeFloat(StructFieldText(e, 'DATA', 'Light Fade End', '0'), '0.0') + '],');
  AddKV(sl, '  ', 'lightingDirectionalXY', SafeInt(StructFieldText(e, 'DATA', 'Directional XY', '0'), '0'));
  AddKV(sl, '  ', 'lightingDirectionalZ', SafeInt(StructFieldText(e, 'DATA', 'Directional Z', '0'), '0'));
  AddKV(sl, '  ', 'lightingInheritanceFlags', IntToStr(GetElementNativeValues(e, 'DATA\Inheritance Flags')));
end;

procedure CollectShoutVariations(sl: TStringList; node: IInterface; recoveries: TStringList; var written: Integer);
var
  i: Integer;
  child, wordElement, spellElement, recoveryElement, wordRef, spellRef: IInterface;
  nodeName, recovery: string;
begin
  if not Assigned(node) or (written >= 3) then Exit;
  nodeName := LowerCase(Name(node));
  if (Signature(node) = 'SNAM') or (Pos('word', nodeName) > 0) or (Pos('variation', nodeName) > 0) then begin
    wordElement := FindDescendantByExactName(node, 'Word');
    if not Assigned(wordElement) then wordElement := FindDescendantByNameContains(node, 'Word of Power');
    spellElement := FindDescendantByExactName(node, 'Spell');
    recoveryElement := FindDescendantByNameContains(node, 'Recovery');
    wordRef := FirstLinkedInElement(wordElement); spellRef := FirstLinkedInElement(spellElement);
    if Assigned(wordRef) or Assigned(spellRef) then begin
      Inc(written);
      if Assigned(wordRef) then AddFormRefKV(sl, 'shoutWord' + IntToStr(written), wordRef);
      if Assigned(spellRef) then AddFormRefKV(sl, 'shoutSpell' + IntToStr(written), spellRef);
      recovery := '0.0';
      if Assigned(recoveryElement) then recovery := SafeFloat(GetEditValue(recoveryElement), '0.0');
      recoveries.Add(recovery);
      Exit;
    end;
  end;
  for i := 0 to ElementCount(node) - 1 do begin
    child := ElementByIndex(node, i);
    CollectShoutVariations(sl, child, recoveries, written);
    if written >= 3 then Exit;
  end;
end;

procedure AddShoutFields(sl: TStringList; e: IInterface);
var
  recoveries: TStringList;
  written: Integer;
begin
  AddStringKV(sl, '  ', 'fullName', GetText(e, 'FULL - Name'));
  AddStringKV(sl, '  ', 'description', GetText(e, 'DESC - Description'));
  AddFormRefKV(sl, 'equipSlot', LinkedByPath(e, 'ETYP - Equipment Type'));
  AddFormRefKV(sl, 'menuDisplayObject', LinkedByPath(e, 'MDOB - Menu Display Object'));
  AddKV(sl, '  ', 'recordFlags', ElementIntegerText(FirstElementByPath(e, 'Record Header\Record Flags', 'Record Header\Flags', 'Record Flags', ''), '0'));
  recoveries := TStringList.Create;
  try
    written := 0;
    CollectShoutVariations(sl, e, recoveries, written);
    while recoveries.Count < 3 do recoveries.Add('0.0');
    sl.Add('  "shoutRecoveryTimes": [' + recoveries[0] + ', ' + recoveries[1] + ', ' + recoveries[2] + '],');
  finally
    recoveries.Free;
  end;
end;

procedure WriteLeveledEntries(sl: TStringList; node: IInterface; var written: Integer);
var
  i: Integer;
  child, lvlo, levelElement, countElement, entryForm, coed, ownerForm, globalForm: IInterface;
  levelValue, countValue, requiredRank, healthMult: string;
begin
  if not Assigned(node) then Exit;
  lvlo := nil;
  if Pos('lvlo', LowerCase(Name(node))) > 0 then lvlo := node;
  if not Assigned(lvlo) then lvlo := FindChildByNameContains(node, 'LVLO');
  if Assigned(lvlo) then begin
    entryForm := FirstLinkedInElement(lvlo);
    levelElement := FindDescendantByExactName(lvlo, 'Level');
    countElement := FindDescendantByExactName(lvlo, 'Count');
    if Assigned(entryForm) and Assigned(levelElement) and Assigned(countElement) then begin
      levelValue := GetEditValue(levelElement); countValue := GetEditValue(countElement);
      coed := FindChildByNameContains(node, 'COED'); ownerForm := nil; globalForm := nil; requiredRank := '0'; healthMult := '100.0';
      if Assigned(coed) then begin
        ownerForm := StructFieldLinked(node, 'COED', 'Owner'); globalForm := StructFieldLinked(node, 'COED', 'Global');
        requiredRank := StructFieldText(node, 'COED', 'Required Rank', '0'); healthMult := StructFieldText(node, 'COED', 'Item Condition', '100.0');
      end;
      sl.Add('    {'); AddKV(sl, '      ', 'form', FormRefJson(entryForm)); AddKV(sl, '      ', 'level', JsonInt(levelValue, '1')); AddKV(sl, '      ', 'count', JsonInt(countValue, '1'));
      if Assigned(ownerForm) then AddKV(sl, '      ', 'owner', FormRefJson(ownerForm));
      if Assigned(globalForm) then AddKV(sl, '      ', 'conditionGlobal', FormRefJson(globalForm));
      AddKV(sl, '      ', 'requiredRank', JsonInt(requiredRank, '0')); AddKV(sl, '      ', 'healthMult', JsonFloat(healthMult, '100.0')); RemoveTrailingComma(sl); sl.Add('    },'); Inc(written); Exit;
    end;
  end;
  for i := 0 to ElementCount(node) - 1 do begin child := ElementByIndex(node, i); WriteLeveledEntries(sl, child, written); end;
end;

procedure AddLeveledListFields(sl: TStringList; e: IInterface; includeModel: Boolean);
var
  written: Integer;
begin
  if includeModel then AddStringKV(sl, '  ', 'modelPath', GetText(e, 'Model\MODL - Model FileName'));
  AddKV(sl, '  ', 'leveledChanceNone', SafeInt(FirstText(e, 'LVLD - Chance None', 'LVLD', ''), '0'));
  AddKV(sl, '  ', 'leveledFlags', ElementIntegerText(FirstElementByPath(e, 'LVLF - Flags', 'LVLF', 'Flags', ''), '0'));
  AddFormRefKV(sl, 'leveledChanceGlobal', LinkedByPath(e, 'LVLG - Chance Global'));
  sl.Add('  "leveledEntries": ['); written := 0; WriteLeveledEntries(sl, e, written); RemoveTrailingComma(sl); sl.Add('  ],');
end;

procedure AddActionFields(sl: TStringList; e: IInterface);
begin
  AddKV(sl, '  ', 'actionIndex', SafeInt(FirstText(e, 'INDX - Index', 'INDX', ''), '0'));
end;

procedure AddMenuIconFields(sl: TStringList; e: IInterface);
begin
  AddStringKV(sl, '  ', 'inventoryIcon', FirstText(e, 'ICON - Menu Icon', 'ICON - Icon', 'ICON'));
end;

procedure AddEyesFields(sl: TStringList; e: IInterface);
begin
  AddStringKV(sl, '  ', 'fullName', GetText(e, 'FULL - Name'));
  AddStringKV(sl, '  ', 'eyesTexture', FirstText(e, 'ICON - Texture', 'ICON - Eye Texture', 'ICON'));
  AddKV(sl, '  ', 'eyesFlags', ElementIntegerText(FirstElementByPath(e, 'DATA - Flags', 'DATA', 'Flags', ''), '1'));
  AddKV(sl, '  ', 'recordFlags', ElementIntegerText(FirstElementByPath(e, 'Record Header\Record Flags', 'Record Header\Flags', 'Record Flags', ''), '0'));
end;

procedure AddNoteFields(sl: TStringList; e: IInterface);
begin
  AddStringKV(sl, '  ', 'fullName', GetText(e, 'FULL - Name'));
  AddStringKV(sl, '  ', 'modelPath', GetText(e, 'Model\MODL - Model FileName'));
  AddStringKV(sl, '  ', 'inventoryIcon', FirstText(e, 'ICON - Inventory Image', 'ICON - Icon', 'ICON'));
  AddFormRefKV(sl, 'pickupSound', LinkedByPath(e, 'YNAM - Pick Up Sound'));
  AddFormRefKV(sl, 'putdownSound', LinkedByPath(e, 'ZNAM - Put Down Sound'));
end;

procedure AddAnimatedObjectFields(sl: TStringList; e: IInterface);
begin
  AddStringKV(sl, '  ', 'modelPath', GetText(e, 'Model\MODL - Model FileName'));
  AddStringKV(sl, '  ', 'animatedUnloadEvent', FirstText(e, 'BNAM - Unload Event', 'BNAM', ''));
end;

procedure AddLoadScreenFields(sl: TStringList; e: IInterface);
begin
  AddStringKV(sl, '  ', 'loadScreenText', FirstText(e, 'DESC - Loading Text', 'DESC - Description', 'DESC'));
  AddFormRefKV(sl, 'loadScreenObject', LinkedByPath(e, 'NNAM - Load Screen NIF'));
  AddKV(sl, '  ', 'loadScreenInitialScale', SafeFloat(FirstText(e, 'SNAM - Initial Scale', 'SNAM', ''), '1.0'));
  sl.Add('  "loadScreenRotationConstraints": [' + SafeInt(StructFieldText(e, 'RNAM', 'X', '0'), '0') + ', ' + SafeInt(StructFieldText(e, 'RNAM', 'Y', '0'), '0') + ', ' + SafeInt(StructFieldText(e, 'RNAM', 'Z', '0'), '0') + '],');
  sl.Add('  "loadScreenRotationOffsetConstraints": [' + SafeInt(StructFieldText(e, 'ONAM', 'Min', '0'), '0') + ', ' + SafeInt(StructFieldText(e, 'ONAM', 'Max', '0'), '0') + '],');
  sl.Add('  "loadScreenTranslationOffset": [' + SafeFloat(StructFieldText(e, 'XNAM', 'X', '0'), '0.0') + ', ' + SafeFloat(StructFieldText(e, 'XNAM', 'Y', '0'), '0.0') + ', ' + SafeFloat(StructFieldText(e, 'XNAM', 'Z', '0'), '0.0') + '],');
  AddStringKV(sl, '  ', 'loadScreenCameraPath', FirstText(e, 'Camera Path\MOD2 - Model FileName', 'MOD2 - Camera Path', 'MOD2'));
  AddKV(sl, '  ', 'recordFlags', ElementIntegerText(FirstElementByPath(e, 'Record Header\Record Flags', 'Record Header\Flags', 'Record Flags', ''), '0'));
  AddPerkConditionsArray(sl, 'conditions', FirstElementByPath(e, 'Conditions', 'Conditions (sorted)', 'CTDA - Conditions', ''), '  ');
end;

procedure AddShaderParticleGeometryFields(sl: TStringList; e: IInterface);
begin
  sl.Add('  "shaderParticleSettings": [' +
    SafeFloat(StructFieldText(e, 'DATA', 'Gravity Velocity', '0'), '0.0') + ', ' + SafeFloat(StructFieldText(e, 'DATA', 'Rotation Velocity', '0'), '0.0') + ', ' +
    SafeFloat(StructFieldText(e, 'DATA', 'Particle Size X', '0'), '0.0') + ', ' + SafeFloat(StructFieldText(e, 'DATA', 'Particle Size Y', '0'), '0.0') + ', ' +
    SafeFloat(StructFieldText(e, 'DATA', 'Center Offset Min', '0'), '0.0') + ', ' + SafeFloat(StructFieldText(e, 'DATA', 'Center Offset Max', '0'), '0.0') + ', ' +
    SafeFloat(StructFieldText(e, 'DATA', 'Start Rotation Range', '0'), '0.0') + ', ' + SafeInt(StructFieldText(e, 'DATA', 'Subtextures X', '1'), '1') + ', ' +
    SafeInt(StructFieldText(e, 'DATA', 'Subtextures Y', '1'), '1') + ', ' + SafeInt(StructFieldText(e, 'DATA', 'Particle Type', '0'), '0') + ', ' +
    SafeFloat(StructFieldText(e, 'DATA', 'Box Size', '0'), '0.0') + ', ' + SafeFloat(StructFieldText(e, 'DATA', 'Particle Density', '0'), '0.0') + '],');
  AddStringKV(sl, '  ', 'shaderParticleTexture', FirstText(e, 'ICON - Particle Texture', 'ICON - Texture', 'ICON'));
end;

procedure AddAddonNodeFields(sl: TStringList; e: IInterface);
begin
  AddStringKV(sl, '  ', 'modelPath', GetText(e, 'Model\MODL - Model FileName'));
  AddKV(sl, '  ', 'addonIndex', SafeInt(FirstText(e, 'DATA - Node Index', 'DATA', ''), '0'));
  AddFormRefKV(sl, 'addonSound', LinkedByPath(e, 'SNAM - Sound'));
  AddKV(sl, '  ', 'addonMasterParticleCap', SafeInt(StructFieldText(e, 'DNAM', 'Master Particle Cap', '0'), '0'));
  AddKV(sl, '  ', 'addonFlags', ElementIntegerText(StructFieldElement(e, 'DNAM', 'Flags'), '0'));
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
    AddStringKV(sl, '  ', 'packageName', PackageName);

    if sig = 'GLOB' then begin
      AddStringKV(sl, '  ', 'globalType', GlobalTypeFromRecord(e));
      AddKV(sl, '  ', 'defaultValue', SafeFloat(GetText(e, 'FLTV - Value'), '0.0'));
    end
    else if sig = 'FLST' then AddFormListFields(sl, e)
    else if sig = 'EQUP' then AddEquipSlotFields(sl, e)
    else if sig = 'VTYP' then AddVoiceTypeFields(sl, e)
    else if sig = 'OTFT' then AddOutfitFields(sl, e)
    else if sig = 'ARMA' then AddArmorTypeFields(sl, e)
    else if sig = 'ARMO' then AddArmorFields(sl, e)
    else if sig = 'BOOK' then AddBookFields(sl, e)
    else if sig = 'MISC' then AddMiscFields(sl, e)
    else if sig = 'KEYM' then AddMiscFields(sl, e)
    else if sig = 'SLGM' then AddSoulGemFields(sl, e)
    else if sig = 'MATT' then AddMaterialTypeFields(sl, e)
    else if sig = 'AMMO' then AddAmmoFields(sl, e)
    else if sig = 'WEAP' then AddWeaponFields(sl, e)
    else if sig = 'ALCH' then AddAlchemyFields(sl, e)
    else if sig = 'INGR' then AddIngredientFields(sl, e)
    else if sig = 'SPEL' then AddSpellFields(sl, e)
    else if sig = 'ENCH' then AddEnchantmentFields(sl, e)
    else if sig = 'SCRL' then AddScrollFields(sl, e)
    else if sig = 'PROJ' then AddProjectileFields(sl, e)
    else if sig = 'TXST' then AddTextureSetFields(sl, e)
    else if sig = 'HAZD' then AddHazardFields(sl, e)
    else if sig = 'IPCT' then AddImpactDataFields(sl, e)
    else if sig = 'RFCT' then AddReferenceEffectFields(sl, e)
    else if sig = 'DUAL' then AddDualCastDataFields(sl, e)
    else if sig = 'STAT' then AddStaticFields(sl, e)
    else if sig = 'MSTT' then AddMovableStaticFields(sl, e)
    else if sig = 'DOOR' then AddDoorFields(sl, e)
    else if sig = 'CSTY' then AddCombatStyleFields(sl, e)
    else if sig = 'SNCT' then AddSoundCategoryFields(sl, e)
    else if sig = 'CLAS' then AddClassFields(sl, e)
    else if sig = 'FLOR' then AddFloraFields(sl, e)
    else if sig = 'TREE' then AddTreeFields(sl, e)
    else if sig = 'COBJ' then AddConstructibleObjectFields(sl, e)
    else if sig = 'CONT' then AddContainerFields(sl, e)
    else if sig = 'IPDS' then AddImpactDataSetFields(sl, e)
    else if sig = 'COLL' then AddCollisionLayerFields(sl, e)
    else if sig = 'FSTP' then AddFootstepFields(sl, e)
    else if sig = 'FSTS' then AddFootstepSetFields(sl, e)
    else if sig = 'REVB' then AddReverbFields(sl, e)
    else if sig = 'ASPC' then AddAcousticSpaceFields(sl, e)
    else if sig = 'APPA' then AddApparatusFields(sl, e)
    else if sig = 'SCOL' then AddStaticCollectionFields(sl, e)
    else if sig = 'GRAS' then AddGrassFields(sl, e)
    else if sig = 'IDLM' then AddIdleMarkerFields(sl, e)
    else if sig = 'ECZN' then AddEncounterZoneFields(sl, e)
    else if sig = 'RELA' then AddRelationshipFields(sl, e)
    else if sig = 'ASTP' then AddAssociationTypeFields(sl, e)
    else if sig = 'MOVT' then AddMovementTypeFields(sl, e)
    else if sig = 'WOOP' then AddWordOfPowerFields(sl, e)
    else if sig = 'WATR' then AddWaterFields(sl, e)
    else if sig = 'IMGS' then AddImageSpaceFields(sl, e)
    else if sig = 'LGTM' then AddLightingTemplateFields(sl, e)
    else if sig = 'SHOU' then AddShoutFields(sl, e)
    else if sig = 'LVLI' then AddLeveledListFields(sl, e, False)
    else if sig = 'LVLN' then AddLeveledListFields(sl, e, True)
    else if sig = 'LVSP' then AddLeveledListFields(sl, e, False)
    else if sig = 'AACT' then AddActionFields(sl, e)
    else if sig = 'MICN' then AddMenuIconFields(sl, e)
    else if sig = 'EYES' then AddEyesFields(sl, e)
    else if sig = 'NOTE' then AddNoteFields(sl, e)
    else if sig = 'ANIO' then AddAnimatedObjectFields(sl, e)
    else if sig = 'LSCR' then AddLoadScreenFields(sl, e)
    else if sig = 'SPGD' then AddShaderParticleGeometryFields(sl, e)
    else if sig = 'ADDN' then AddAddonNodeFields(sl, e)
    else if sig = 'MGEF' then AddMagicEffectFields(sl, e)
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
var
  packageFolder: string;
begin
  ExportedCount := 0;
  SkippedCount := 0;

  DebugNpcFlags := False;
  DebugNpcEditorID := '';
  // Use DebugNpcEditorID := ''; to log every selected NPC.

  PackageName := 'DFG_Import';
  if not InputQuery('Dynamic Forms Generator', 'Package name (creates/updates package.db when DFG loads):', PackageName) then begin
    AddMessage('[DFG] Export cancelled: no package selected.');
    Result := 1;
    Exit;
  end;

  PackageName := Trim(PackageName);
  if PackageName = '' then
    PackageName := 'DFG_Import';

  packageFolder := SafePackageFolder(PackageName);
  PackageDir := wbDataPath + 'Viny Mods\Dynamic Forms Generator\Packages\' + packageFolder + '\';
  OutputDir := PackageDir + 'imports\';
  ForceDirectories(PackageDir);
  ForceDirectories(OutputDir);
  WritePackageManifest;

  AddMessage('[DFG] Package: ' + PackageName);
  AddMessage('[DFG] Package folder: ' + PackageDir);
  AddMessage('[DFG] Import folder: ' + OutputDir);
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
