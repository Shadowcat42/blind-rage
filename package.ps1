param(
 [string]$BuildDirectory=(Join-Path $PSScriptRoot 'dist')
)
$ErrorActionPreference='Stop'
$distribution=(Resolve-Path -LiteralPath $BuildDirectory).Path
$stage=Join-Path $PSScriptRoot ('build/package-'+[Guid]::NewGuid().ToString('N'))
try {
 $mod=Join-Path $stage 'mods/accessibility/dist'
 New-Item -ItemType Directory -Force -Path $mod,(Join-Path $PSScriptRoot 'artifacts') | Out-Null
 # Explicit allowlist: never package the game directory or all of dist.
 Copy-Item -LiteralPath (Join-Path $distribution 'BlindRage.exe') -Destination $stage
 foreach($name in @('asura_accessibility.dll','prism.dll')){Copy-Item -LiteralPath (Join-Path $distribution $name) -Destination $mod}
 # No accessibility.ini: BlindRage.exe creates it from built-in defaults, so
 # extracting an update never replaces a player's settings.
 Copy-Item -LiteralPath (Join-Path $distribution 'licenses') -Destination $mod -Recurse
 # Documentation goes beside the mod rather than into the game's root folder.
 $docs=Split-Path -Parent $mod
 foreach($name in @('README.md','CHANGELOG.md','THIRD_PARTY_NOTICES.md','LICENSE','LICENSE-EXCEPTION.txt')){Copy-Item -LiteralPath (Join-Path $PSScriptRoot $name) -Destination $docs}
 New-Item -ItemType Directory -Force -Path (Join-Path $docs 'docs') | Out-Null
 foreach($name in @('SHOOTING_BEACONS.md','MELEE_AIM.md','WALK_BEACON.md','LOOK_AROUND.md','ROADMAP.md')){
  Copy-Item -LiteralPath (Join-Path $PSScriptRoot ('docs/'+$name)) -Destination (Join-Path $docs 'docs')
 }
 $archive=Join-Path $PSScriptRoot 'artifacts/blind-rage-windows-x64.zip'
 Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $archive -Force
 Write-Output $archive
} finally {
 # Delete only our generated package directory, including when packaging fails.
 $buildRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot 'build'))
 $resolvedStage=[IO.Path]::GetFullPath($stage)
 if([IO.Path]::GetDirectoryName($resolvedStage) -ne $buildRoot -or
    [IO.Path]::GetFileName($resolvedStage) -notmatch '^package-[0-9a-f]{32}$'){
  throw 'Refusing to remove a package staging directory outside this project build folder.'
 }
 if(Test-Path -LiteralPath $resolvedStage){Remove-Item -LiteralPath $resolvedStage -Recurse -Force}
}
