param([switch]$Test,[string]$InstallTo)
$ErrorActionPreference='Stop'
Push-Location $PSScriptRoot
try {
 $compiler=Join-Path $PSScriptRoot 'tools/llvm-mingw-20261117-ucrt-x86_64/bin/x86_64-w64-mingw32-clang++.exe'
 if(!(Test-Path -LiteralPath $compiler)){throw 'Portable compiler missing; see README.md'}
 New-Item -ItemType Directory -Force dist | Out-Null
 $common=@('-std=c++20','-O2','-Wall','-Wextra','-static','-Ivendor/include')
 & $compiler @common '-shared' 'src/dll.cpp' '-o' 'dist/asura_accessibility.dll' '-lbcrypt' '-lole32' '-luser32'
 if($LASTEXITCODE){throw 'DLL compilation failed'}
 & $compiler @common '-municode' 'src/launcher.cpp' '-o' 'dist/AsurasWrathAccessible.exe' '-lbcrypt' '-luser32'
 if($LASTEXITCODE){throw 'Launcher compilation failed'}
 Copy-Item -LiteralPath dist/AsurasWrathAccessible.exe -Destination dist/BlindRage.exe
 & $compiler @common 'tests/reader_test.cpp' '-o' 'dist/reader_test.exe'
 if($LASTEXITCODE){throw 'Test compilation failed'}
 & $compiler @common '-municode' 'tests/speech_test.cpp' '-o' 'dist/speech_test.exe' '-lbcrypt' '-lole32' '-luser32'
 if($LASTEXITCODE){throw 'Speech test compilation failed'}
 Copy-Item -LiteralPath vendor/dynamic/release/bin/prism.dll -Destination dist/prism.dll
 if(!(Test-Path -LiteralPath dist/accessibility.ini)){Copy-Item -LiteralPath accessibility.ini -Destination dist/accessibility.ini}
 New-Item -ItemType Directory -Force dist/licenses | Out-Null
 Copy-Item -LiteralPath vendor/NOTICE -Destination dist/licenses/Prism-NOTICE.txt
 Copy-Item -LiteralPath vendor/LICENSES -Destination dist/licenses -Recurse -Force
 Copy-Item -LiteralPath tools/llvm-mingw-20261117-ucrt-x86_64/LICENSE.TXT -Destination dist/licenses/LLVM-LICENSE.txt
 if($Test){& ./dist/reader_test.exe; if($LASTEXITCODE){throw 'Reader tests failed'}}
 if(!$InstallTo -and (Test-Path -LiteralPath ../../bin_disc/asura_wrath_recomp.exe)){$InstallTo=(Resolve-Path ../..).Path}
 if($InstallTo){& ./install.ps1 -GameRoot $InstallTo}
} finally {Pop-Location}
