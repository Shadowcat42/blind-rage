param([Parameter(Mandatory=$true)][string]$GameRoot)
$ErrorActionPreference='Stop'
$root=(Resolve-Path -LiteralPath $GameRoot).Path
if(!(Test-Path -LiteralPath (Join-Path $root 'bin_disc/asura_wrath_recomp.exe'))){throw 'Install the supported recomp and your game data separately first.'}
$source=Join-Path $PSScriptRoot 'dist'
$target=Join-Path $root 'mods/accessibility/dist'
New-Item -ItemType Directory -Force -Path $target | Out-Null
if([IO.Path]::GetFullPath($source) -ne [IO.Path]::GetFullPath($target)){
 foreach($name in @('asura_accessibility.dll','prism.dll')){Copy-Item -LiteralPath (Join-Path $source $name) -Destination (Join-Path $target $name)}
 Copy-Item -LiteralPath (Join-Path $source 'licenses') -Destination $target -Recurse -Force
 if(!(Test-Path -LiteralPath (Join-Path $target 'accessibility.ini'))){Copy-Item -LiteralPath (Join-Path $source 'accessibility.ini') -Destination $target}
}
Copy-Item -LiteralPath (Join-Path $source 'BlindRage.exe') -Destination (Join-Path $root 'BlindRage.exe')
if(Test-Path -LiteralPath (Join-Path $root 'AsurasWrathAccessible.exe')){Copy-Item -LiteralPath (Join-Path $source 'BlindRage.exe') -Destination (Join-Path $root 'AsurasWrathAccessible.exe')}
Write-Output 'Blind Rage installed. Original game and recomp binaries were not replaced.'
