param([string]$BuildDirectory=(Join-Path $PSScriptRoot 'dist'))
$ErrorActionPreference='Stop'
$distribution=(Resolve-Path -LiteralPath $BuildDirectory).Path
$stage=Join-Path $PSScriptRoot ('build/package-'+[Guid]::NewGuid().ToString('N'))
$mod=Join-Path $stage 'mods/accessibility/dist'
New-Item -ItemType Directory -Force -Path $mod,(Join-Path $PSScriptRoot 'artifacts') | Out-Null
# Explicit allowlist: never package the game directory or all of dist.
Copy-Item -LiteralPath (Join-Path $distribution 'BlindRage.exe') -Destination $stage
foreach($name in @('asura_accessibility.dll','prism.dll')){Copy-Item -LiteralPath (Join-Path $distribution $name) -Destination $mod}
# Ship project defaults; do not export a developer's preserved local INI.
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'accessibility.ini') -Destination (Split-Path -Parent $mod)
Copy-Item -LiteralPath (Join-Path $distribution 'licenses') -Destination $mod -Recurse
foreach($name in @('README.md','THIRD_PARTY_NOTICES.md','LICENSE')){if(Test-Path -LiteralPath (Join-Path $PSScriptRoot $name)){Copy-Item -LiteralPath (Join-Path $PSScriptRoot $name) -Destination $stage}}
New-Item -ItemType Directory -Force -Path (Join-Path $stage 'docs') | Out-Null
foreach($name in @('HOOK_RESEARCH.md','ROADMAP.md','GAMEPLAY_TEXT_READER.md','GAMEPLAY_CUE_RESEARCH.md','QTE_INPUT_RESEARCH.md','INPUT_PROMPTS.md','EPISODE_1_GROUNDWORK.md','EPISODE_1_TESTING.md')){
 Copy-Item -LiteralPath (Join-Path $PSScriptRoot ('docs/'+$name)) -Destination (Join-Path $stage 'docs')
}
$archive=Join-Path $PSScriptRoot 'artifacts/blind-rage-windows-x64.zip'
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $archive -Force
Write-Output $archive
