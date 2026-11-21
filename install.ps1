param([Parameter(Mandatory=$true)][string]$GameRoot)
$ErrorActionPreference='Stop'
$root=(Resolve-Path -LiteralPath $GameRoot).Path
if(!(Test-Path -LiteralPath (Join-Path $root 'bin_disc/asura_wrath_recomp.exe')) -and !(Test-Path -LiteralPath (Join-Path $root 'bin/asura_wrath_recomp.exe'))){throw 'Install the supported recomp and your game data separately first.'}
$source=Join-Path $PSScriptRoot 'dist'
$target=Join-Path $root 'mods/accessibility/dist'
New-Item -ItemType Directory -Force -Path $target | Out-Null
if([IO.Path]::GetFullPath($source) -ne [IO.Path]::GetFullPath($target)){
 foreach($name in @('asura_accessibility.dll','prism.dll')){Copy-Item -LiteralPath (Join-Path $source $name) -Destination (Join-Path $target $name)}
 Copy-Item -LiteralPath (Join-Path $source 'licenses') -Destination $target -Recurse -Force
 # The DLL reads mods/accessibility/accessibility.ini. Add new defaults to an
 # existing file without changing the installation's own choices.
 & (Join-Path $PSScriptRoot 'sync_ini.ps1') -Template (Join-Path $PSScriptRoot 'config/accessibility.ini') -Target (Join-Path $root 'mods/accessibility/accessibility.ini')
}
Copy-Item -LiteralPath (Join-Path $source 'BlindRage.exe') -Destination (Join-Path $root 'BlindRage.exe')
Write-Output 'Blind Rage installed. Original game and recomp binaries were not replaced.'
