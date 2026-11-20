param([switch]$Test,[string]$InstallTo,[switch]$NoInstall)
$ErrorActionPreference='Stop'
Push-Location $PSScriptRoot
try {
 $compiler=Join-Path $PSScriptRoot 'tools/llvm-mingw-20261117-ucrt-x86_64/bin/x86_64-w64-mingw32-clang++.exe'
 if(!(Test-Path -LiteralPath $compiler)){throw 'Portable compiler missing; see README.md'}
 New-Item -ItemType Directory -Force dist | Out-Null
 $common=@('-std=c++20','-O2','-Wall','-Wextra','-static','-Ivendor/include')
 $hookRoot='vendor/minhook-1.3.4'
 if(!(Test-Path -LiteralPath "$hookRoot/src/hook.c")){throw 'MinHook missing; run bootstrap.ps1'}
 New-Item -ItemType Directory -Force build/hooks | Out-Null
 $ccompiler=$compiler.Replace('clang++.exe','clang.exe')
 $hookObjects=@()
 foreach($source in @('buffer','hook','trampoline','hde/hde64')){
  $object='build/hooks/'+$source.Replace('/','-')+'.o'
  & $ccompiler '-O2' '-c' "$hookRoot/src/$source.c" "-I$hookRoot/include" '-o' $object
  if($LASTEXITCODE){throw 'MinHook compilation failed'}
  $hookObjects+=$object
 }
 & $compiler @common "-I$hookRoot/include" 'tests/hook_test.cpp' @hookObjects '-o' 'build/hooks/hook_test.exe'
 if($LASTEXITCODE){throw 'Hook test compilation failed'}
 & $compiler @common 'tests/ui_changes_test.cpp' '-o' 'build/hooks/ui_changes_test.exe'
 if($LASTEXITCODE){throw 'UI change test compilation failed'}
 & $compiler @common "-I$hookRoot/include" 'tests/ui_hook_source_test.cpp' @hookObjects '-o' 'build/hooks/ui_hook_source_test.exe' '-luser32'
 if($LASTEXITCODE){throw 'UI hook source test compilation failed'}
 New-Item -ItemType Directory -Force build/speech | Out-Null
 & $compiler @common '-shared' 'tests/fake_prism.cpp' '-o' 'build/speech/prism.dll'
 if($LASTEXITCODE){throw 'Fake speech backend compilation failed'}
 & $compiler @common 'tests/diagnostics_test.cpp' '-o' 'build/speech/diagnostics_test.exe' '-lbcrypt' '-luser32'
 if($LASTEXITCODE){throw 'Diagnostic test compilation failed'}
 & $compiler @common 'tests/build_profile_test.cpp' '-o' 'build/hooks/build_profile_test.exe'
 if($LASTEXITCODE){throw 'Build profile test compilation failed'}
 & $compiler @common 'tests/gameplay_text_test.cpp' '-o' 'build/hooks/gameplay_text_test.exe'
 if($LASTEXITCODE){throw 'Gameplay text test compilation failed'}
 & $compiler @common "-I$hookRoot/include" 'tests/presentation_hooks_test.cpp' @hookObjects '-o' 'build/hooks/presentation_hooks_test.exe' '-luser32'
 if($LASTEXITCODE){throw 'Presentation hook test compilation failed'}
 & $compiler @common "-I$hookRoot/include" '-shared' 'src/dll.cpp' @hookObjects '-o' 'dist/asura_accessibility.dll' '-lbcrypt' '-lole32' '-luser32'
 if($LASTEXITCODE){throw 'DLL compilation failed'}
 & $compiler @common '-municode' 'src/launcher.cpp' '-o' 'dist/BlindRage.exe' '-lbcrypt' '-luser32'
 if($LASTEXITCODE){throw 'Launcher compilation failed'}
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
 Copy-Item -LiteralPath "$hookRoot/LICENSE.txt" -Destination dist/licenses/MinHook-LICENSE.txt
 if($Test){
  & ./dist/reader_test.exe; if($LASTEXITCODE){throw 'Reader tests failed'}
  & ./build/hooks/hook_test.exe; if($LASTEXITCODE){throw 'Hook tests failed'}
  & ./build/hooks/ui_changes_test.exe; if($LASTEXITCODE){throw 'UI change tests failed'}
  & ./build/hooks/ui_hook_source_test.exe; if($LASTEXITCODE){throw 'UI hook source tests failed'}
  & ./build/speech/diagnostics_test.exe; if($LASTEXITCODE){throw 'Diagnostic tests failed'}
  & ./build/hooks/build_profile_test.exe; if($LASTEXITCODE){throw 'Build profile tests failed'}
  & ./build/hooks/gameplay_text_test.exe; if($LASTEXITCODE){throw 'Gameplay text tests failed'}
  & ./build/hooks/presentation_hooks_test.exe; if($LASTEXITCODE){throw 'Presentation hook tests failed'}
 }
 if(!$NoInstall){
  if(!$InstallTo -and ((Test-Path -LiteralPath ../../bin_disc/asura_wrath_recomp.exe) -or (Test-Path -LiteralPath ../../bin/asura_wrath_recomp.exe))){$InstallTo=(Resolve-Path ../..).Path}
  if($InstallTo){& ./install.ps1 -GameRoot $InstallTo}
 }
} finally {Pop-Location}
