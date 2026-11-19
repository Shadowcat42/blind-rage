$ErrorActionPreference='Stop'
$ProgressPreference='SilentlyContinue'
Push-Location $PSScriptRoot
try {
 $assets=@(
  @{Dir='tools';Name='llvm-mingw-20261117-ucrt-x86_64.zip';Url='https://github.com/mstorsjo/llvm-mingw/releases/download/20261117/llvm-mingw-20261117-ucrt-x86_64.zip';Hash='4d002e330a0f703625aa5ccbc37e96f8f824413e163bfc7336774aabd2c3a412'},
  @{Dir='vendor';Name='prism-windows-x64.zip';Url='https://github.com/ethindp/prism/releases/download/v0.18.3/prism-windows-x64.zip';Hash='13d3c9d1d524b0737cde261c6468845753e36b6c5c2637e7d26bfb8bf4d8783f'}
 )
 foreach($asset in $assets){
  New-Item -ItemType Directory -Force $asset.Dir | Out-Null
  $path=Join-Path $asset.Dir $asset.Name
  if(!(Test-Path -LiteralPath $path)){Invoke-WebRequest -Uri $asset.Url -OutFile $path}
  if((Get-FileHash -Algorithm SHA256 -LiteralPath $path).Hash.ToLowerInvariant() -ne $asset.Hash){throw ('Checksum mismatch: '+$asset.Name)}
  Expand-Archive -LiteralPath $path -DestinationPath $asset.Dir -Force
 }
} finally {Pop-Location}
