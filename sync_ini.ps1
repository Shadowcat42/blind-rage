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
