unit userscript;

var
  OutputDir: string;
  ExportedCount: Integer;
  SkippedCount: Integer;

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

function SafeInt(const s: string; fallback: string): string;
begin
  Result := Trim(s);
  if Result = '' then
    Result := fallback;
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

function ColorChannel(e: IInterface; path: string; fallback: string): string;
begin
  Result := SafeInt(GetText(e, path), fallback);
end;

function FormKindForSignature(sig: string): string;
begin
  if sig = 'GLOB' then Result := 'Global'
  else if sig = 'KYWD' then Result := 'Keyword'
  else if sig = 'OTFT' then Result := 'Outfit'
  else if sig = 'CLFM' then Result := 'Color'
  else if sig = 'ARTO' then Result := 'ArtObject'
  else if sig = 'PERK' then Result := 'Perk'
  else if sig = 'HDPT' then Result := 'HeadPart'
  else if sig = 'SNDR' then Result := 'SoundDescriptor'
  else if sig = 'LIGH' then Result := 'Light'
  else if sig = 'EXPL' then Result := 'Explosion'
  else if sig = 'ACTI' then Result := 'Activator'
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
      linked := LinksTo(ElementByIndex(arr, i));
      if Assigned(linked) then
        sl.Add('    ' + FormRefJson(linked) + ',');
    end;
  end;
  RemoveTrailingComma(sl);
  sl.Add('  ],');
end;

procedure AddOutfitFields(sl: TStringList; e: IInterface);
begin
  AddFormRefArrayFromElement(sl, 'outfitPieces', ElementByPath(e, 'INAM - Items'));
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

procedure AddPerkFields(sl: TStringList; e: IInterface);
var
  data: string;
begin
  data := GetText(e, 'DATA - Data');
  AddStringKV(sl, '  ', 'fullName', GetText(e, 'FULL - Name'));
  AddStringKV(sl, '  ', 'description', GetText(e, 'DESC - Description'));
  AddKV(sl, '  ', 'trait', BoolJson(HasWord(data, 'Trait')));
  AddKV(sl, '  ', 'level', SafeInt(GetText(e, 'DATA - Data\Level'), '0'));
  AddKV(sl, '  ', 'numRanks', SafeInt(GetText(e, 'DATA - Data\Num Ranks'), '1'));
  AddKV(sl, '  ', 'playable', BoolJson(not HasWord(data, 'Playable: False')));
  AddKV(sl, '  ', 'hidden', BoolJson(HasWord(data, 'Hidden: True')));
  AddFormRefKV(sl, 'nextPerk', LinkedByPath(e, 'NNAM - Next Perk'));
  sl.Add('  "conditions": [],');
  sl.Add('  "entries": [],');
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

procedure AddNPCFields(sl: TStringList; e: IInterface);
var
  flags: string;
begin
  flags := GetText(e, 'ACBS - Configuration\Flags');
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
  AddKV(sl, '  ', 'female', BoolJson(HasWord(flags, 'Female')));
  AddKV(sl, '  ', 'oppositeGenderAnim', BoolJson(HasWord(flags, 'Opposite')));
  AddKV(sl, '  ', 'essential', BoolJson(HasWord(flags, 'Essential')));
  AddKV(sl, '  ', 'protected', BoolJson(HasWord(flags, 'Protected')));
  AddKV(sl, '  ', 'unique', BoolJson(HasWord(flags, 'Unique')));
  AddKV(sl, '  ', 'calcStats', BoolJson(HasWord(flags, 'PC Level Mult') or HasWord(flags, 'Calc')));
  AddKV(sl, '  ', 'respawn', BoolJson(HasWord(flags, 'Respawn')));
  AddKV(sl, '  ', 'doesntAffectStealthMeter', BoolJson(HasWord(flags, 'Stealth')));
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
  AddFormRefArrayFromElement(sl, 'headParts', ElementByPath(e, 'Head Parts'));
  sl.Add('  "tintLayers": [],');
  sl.Add('  "factions": [],');
  sl.Add('  "perks": [],');
  sl.Add('  "spells": [],');
  sl.Add('  "skills": [15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15],');
  sl.Add('  "skillOffsets": [0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0],');
  sl.Add('  "faceMorphs": [0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0],');
  sl.Add('  "faceParts": [0,0,0,0],');
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
    AddStringKV(sl, '  ', 'editorId', edid);

    if sig = 'GLOB' then begin
      AddStringKV(sl, '  ', 'globalType', GlobalTypeFromRecord(e));
      AddKV(sl, '  ', 'defaultValue', SafeFloat(GetText(e, 'FLTV - Value'), '0.0'));
    end
    else if sig = 'OTFT' then AddOutfitFields(sl, e)
    else if sig = 'CLFM' then AddColorFields(sl, e)
    else if sig = 'ARTO' then AddArtObjectFields(sl, e)
    else if sig = 'PERK' then AddPerkFields(sl, e)
    else if sig = 'HDPT' then AddHeadPartFields(sl, e)
    else if sig = 'SNDR' then AddSoundDescriptorFields(sl, e)
    else if sig = 'LIGH' then AddLightFields(sl, e)
    else if sig = 'EXPL' then AddExplosionFields(sl, e)
    else if sig = 'ACTI' then AddActivatorFields(sl, e)
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
  OutputDir := wbDataPath + 'Viny Mods\Dynamic Forms Generator\Forms\';
  ForceDirectories(OutputDir);
  AddMessage('[DFG] Output folder: ' + OutputDir);
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
