# Refresh an installed accessibility.ini from the project template without
# losing local choices. The result follows the template's layout and comments;
# every key already present in the target keeps its value, keys only in the
# target are retained in their section, and keys new to the template appear
# with their defaults. Matching is case-insensitive, like GetPrivateProfile*.
param(
 [Parameter(Mandatory=$true)][string]$Template,
 [Parameter(Mandatory=$true)][string]$Target
)
$ErrorActionPreference='Stop'
function Read-Ini([string[]]$lines){
 $values=@{};$order=[ordered]@{};$section=''
 foreach($line in $lines){
  $text=$line.Trim()
  if($text -match '^\[(.+)\]$'){$section=$Matches[1].Trim().ToLowerInvariant();if(!$order.Contains($section)){$order[$section]=[Collections.Generic.List[string]]::new()};continue}
  if($text -eq '' -or $text.StartsWith(';') -or $text.StartsWith('#')){continue}
  $equals=$text.IndexOf('=');if($equals -lt 1){continue}
  $key=$text.Substring(0,$equals).Trim().ToLowerInvariant()
  # Retired settings: the stability repair is unconditional, and the boosted cone
  # uses multipliers of the game's own limits instead of a flat angle.
  if($section -eq 'gameplay' -and ($key -eq 'stable_lock_cone' -or $key -eq 'shooting_cone_degrees')){continue}
  if(!$order.Contains($section)){$order[$section]=[Collections.Generic.List[string]]::new()}
  $id="$section`n$key"
  # GetPrivateProfile* uses the first occurrence of a key.
  if(!$values.ContainsKey($id)){$values[$id]=$line;$order[$section].Add($key)}
 }
 return @{Values=$values;Order=$order}
}
$templateLines=[IO.File]::ReadAllLines((Resolve-Path -LiteralPath $Template).Path)
if(!(Test-Path -LiteralPath $Target)){
 [IO.File]::WriteAllLines([IO.Path]::GetFullPath($Target),$templateLines,[Text.UTF8Encoding]::new($false))
 Write-Output 'accessibility.ini created from defaults.'
 return
}
$targetPath=(Resolve-Path -LiteralPath $Target).Path
$targetLines=[IO.File]::ReadAllLines($targetPath)
$local=Read-Ini $targetLines
# Migrate before merging defaults, so adding shooting_mode never changes a
# legacy user's assistance choice to the new-install beacon default.
$modeId="gameplay`nshooting_mode";$legacyId="gameplay`nshooting_assist"
if(!$local.Values.ContainsKey($modeId) -and $local.Values.ContainsKey($legacyId)){
 $raw=$local.Values[$legacyId].Split('=',2)[1].Split([char[]]';#')[0].Trim()
 $legacy=0;[void][int]::TryParse($raw,[ref]$legacy)
 $mode=if($legacy -ne 0){'boosted'}else{'off'}
 $local.Values[$modeId]="shooting_mode=$mode"
 $local.Order['gameplay'].Add('shooting_mode')
}
# Rename earlier mode values: native is now off, autoaim is now boosted, and the
# tracking mode is now L1 in every mode (tracking mode itself had beacons).
if($local.Values.ContainsKey($modeId)){
 $raw=$local.Values[$modeId].Split('=',2)[1].Split([char[]]';#')[0].Trim()
 $renamed=@{native='off';autoaim='boosted';tracking='beacons'}
 if($renamed.ContainsKey($raw)){$local.Values[$modeId]="shooting_mode=$($renamed[$raw])"}
}

# Older files store shortcuts as virtual-key codes that always meant Ctrl+Alt.
# Rewrite those as the equivalent names; unknown codes are kept and still work.
$keyNames=@{8='Backspace';9='Tab';13='Enter';19='Pause';27='Escape';32='Space';33='PageUp';34='PageDown';35='End';36='Home';37='Left';38='Up';39='Right';40='Down';44='PrintScreen';45='Insert';46='Delete';93='Menu';106='NumpadMultiply';107='NumpadAdd';109='NumpadSubtract';110='NumpadDecimal';111='NumpadDivide';145='ScrollLock';186='Semicolon';187='Equals';188='Comma';189='Minus';190='Period';191='Slash';192='Backtick';219='LeftBracket';220='Backslash';221='RightBracket';222='Quote'}
for($k=48;$k -le 57;$k++){$keyNames[$k]=[string][char]$k}
for($k=65;$k -le 90;$k++){$keyNames[$k]=[string][char]$k}
for($k=0;$k -le 9;$k++){$keyNames[96+$k]="Numpad$k"}
for($k=1;$k -le 24;$k++){$keyNames[111+$k]="F$k"}
if($local.Order.Contains('hotkeys')){
 foreach($key in $local.Order['hotkeys']){
  $id="hotkeys`n$key";$parts=$local.Values[$id].Split('=',2)
  $raw=$parts[1].Split([char[]]';#')[0].Trim();$code=0
  if($raw -match '^\d{1,3}$' -and [int]::TryParse($raw,[ref]$code)){
   if($code -eq 0){$local.Values[$id]="$($parts[0].Trim())=None"}
   elseif($keyNames.ContainsKey($code)){$local.Values[$id]="$($parts[0].Trim())=Ctrl+Alt+$($keyNames[$code])"}
  }
 }
}

$defaults=Read-Ini $templateLines
$used=@{};$added=0
$output=[Collections.Generic.List[string]]::new()
# Insert target-only keys after the section's last setting, ahead of any
# comments that introduce the next section.
$script:last=-1
function Add-LocalOnly([string]$section){
 if(!$local.Order.Contains($section)){return}
 foreach($key in $local.Order[$section]){$id="$section`n$key";if(!$used.ContainsKey($id)){$script:last++;$output.Insert($script:last,$local.Values[$id]);$used[$id]=$true}}
}
$section=''
foreach($line in $templateLines){
 $text=$line.Trim()
 if($text -match '^\[(.+)\]$'){
  Add-LocalOnly $section
  $section=$Matches[1].Trim().ToLowerInvariant();$output.Add($line);$script:last=$output.Count-1;continue
 }
 $equals=$text.IndexOf('=')
 if($text -ne '' -and !$text.StartsWith(';') -and !$text.StartsWith('#') -and $equals -gt 0){
  $id="$section`n"+$text.Substring(0,$equals).Trim().ToLowerInvariant()
  if($used.ContainsKey($id)){continue}
  $used[$id]=$true
  if($local.Values.ContainsKey($id)){$output.Add($local.Values[$id])}else{$output.Add($line);$added++}
  $script:last=$output.Count-1;continue
 }
 $output.Add($line)
}
Add-LocalOnly $section
foreach($name in $local.Order.Keys){
 if($defaults.Order.Contains($name)){continue}
 $output.Add('');if($name){$output.Add("[$name]")};$script:last=$output.Count-1;Add-LocalOnly $name
}
if((($output -join "`n")) -ne (($targetLines -join "`n"))){
 [IO.File]::WriteAllLines($targetPath,$output,[Text.UTF8Encoding]::new($false))
 Write-Output "accessibility.ini updated; $added new setting(s) added with defaults, existing values kept."
}
