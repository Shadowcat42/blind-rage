$ErrorActionPreference='Stop'
$stage=Join-Path $PSScriptRoot ('build/package-'+[Guid]::NewGuid().ToString('N'))
$mod=Join-Path $stage 'mods/accessibility/dist'
New-Item -ItemType Directory -Force -Path $mod,(Join-Path $PSScriptRoot 'artifacts') | Out-Null
# Explicit allowlist: never package the game directory or all of dist.
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'dist/BlindRage.exe') -Destination $stage
foreach($name in @('asura_accessibility.dll','prism.dll','accessibility.ini')){Copy-Item -LiteralPath (Join-Path $PSScriptRoot ('dist/'+$name)) -Destination $mod}
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'dist/licenses') -Destination $mod -Recurse
foreach($name in @('README.md','THIRD_PARTY_NOTICES.md','LICENSE')){if(Test-Path -LiteralPath (Join-Path $PSScriptRoot $name)){Copy-Item -LiteralPath (Join-Path $PSScriptRoot $name) -Destination $stage}}
$archive=Join-Path $PSScriptRoot 'artifacts/blind-rage-windows-x64.zip'
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $archive -Force
Write-Output $archive
