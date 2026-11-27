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
 New-Item -ItemType Directory -Force build/speech,build/tests | Out-Null
 & $compiler @common '-shared' 'tests/fake_prism.cpp' '-o' 'build/speech/prism.dll'
 if($LASTEXITCODE){throw 'Fake speech backend compilation failed'}
 & $compiler @common 'tests/diagnostics_test.cpp' '-o' 'build/speech/diagnostics_test.exe' '-lbcrypt' '-luser32'
 if($LASTEXITCODE){throw 'Diagnostic test compilation failed'}
 # The launcher embeds the default settings and creates or refreshes the
 # installation's accessibility.ini from them; packages never ship the file.
 New-Item -ItemType Directory -Force build/generated | Out-Null
 $defaults=[IO.File]::ReadAllText((Join-Path $PSScriptRoot 'config/accessibility.ini'))
 if($defaults.Contains(')BRINI"')){throw 'Default settings contain the embedding delimiter'}
 [IO.File]::WriteAllText((Join-Path $PSScriptRoot 'build/generated/default_settings.h'),"#pragma once`nnamespace a11y {`ninline constexpr const char kDefaultSettings[] = R`"BRINI($defaults)BRINI`";`n}`n",[Text.UTF8Encoding]::new($false))
 & $compiler @common '-Ibuild/generated' 'tests/settings_file_test.cpp' '-o' 'build/tests/settings_file_test.exe'
 if($LASTEXITCODE){throw 'Settings file test compilation failed'}
 & $compiler @common 'tests/recomp_setup_test.cpp' '-o' 'build/tests/recomp_setup_test.exe' '-lwinhttp' '-lole32' '-luuid' '-luser32' '-lbcrypt'
 if($LASTEXITCODE){throw 'Recomp setup test compilation failed'}
 & $compiler @common 'tests/fire_warning_test.cpp' '-o' 'build/tests/fire_warning_test.exe'
 if($LASTEXITCODE){throw 'Fire warning test compilation failed'}
 & $compiler @common 'tests/mod_tutorials_test.cpp' '-o' 'build/tests/mod_tutorials_test.exe'
 if($LASTEXITCODE){throw 'Mod tutorial test compilation failed'}
 & $compiler @common 'tests/build_profile_test.cpp' '-o' 'build/hooks/build_profile_test.exe'
 if($LASTEXITCODE){throw 'Build profile test compilation failed'}
 & $compiler @common 'tests/gameplay_text_test.cpp' '-o' 'build/hooks/gameplay_text_test.exe'
 if($LASTEXITCODE){throw 'Gameplay text test compilation failed'}
 & $compiler @common 'tests/recomp_prompts_test.cpp' '-o' 'build/hooks/recomp_prompts_test.exe'
 if($LASTEXITCODE){throw 'Recomp prompt test compilation failed'}
 & $compiler @common 'tests/gameplay_status_test.cpp' '-o' 'build/hooks/gameplay_status_test.exe'
 if($LASTEXITCODE){throw 'Gameplay status test compilation failed'}
 & $compiler @common 'tests/qte_reader_test.cpp' '-o' 'build/hooks/qte_reader_test.exe'
 if($LASTEXITCODE){throw 'QTE reader test compilation failed'}
 & $compiler @common 'tests/synchronic_reader_test.cpp' '-o' 'build/hooks/synchronic_reader_test.exe'
 if($LASTEXITCODE){throw 'Synchronic reader test compilation failed'}
 & $compiler @common "-I$hookRoot/include" 'tests/gameplay_hooks_test.cpp' @hookObjects '-o' 'build/hooks/gameplay_hooks_test.exe'
 if($LASTEXITCODE){throw 'Gameplay hook test compilation failed'}
 & $compiler @common "-I$hookRoot/include" 'tests/presentation_hooks_test.cpp' @hookObjects '-o' 'build/hooks/presentation_hooks_test.exe' '-luser32'
 if($LASTEXITCODE){throw 'Presentation hook test compilation failed'}
 & $compiler @common '-municode' 'tests/restart_test.cpp' '-o' 'build/hooks/restart_test.exe' '-lshell32' '-lbcrypt'
 if($LASTEXITCODE){throw 'Restart test compilation failed'}
 & $compiler @common 'tests/hotkey_names_test.cpp' '-o' 'build/tests/hotkey_names_test.exe'
 if($LASTEXITCODE){throw 'Hotkey name compilation failed'}
 & $compiler @common 'tests/shooting_beacons_test.cpp' '-o' 'build/tests/shooting_beacons_test.exe'
 if($LASTEXITCODE){throw 'Shooting beacon compilation failed'}
 & $compiler @common 'tests/lock_trace_test.cpp' '-o' 'build/hooks/lock_trace_test.exe'
 if($LASTEXITCODE){throw 'Lock trace test compilation failed'}
 & $compiler @common "-I$hookRoot/include" '-shared' 'src/dll.cpp' @hookObjects '-o' 'dist/asura_accessibility.dll' '-lbcrypt' '-lole32' '-luser32'
 if($LASTEXITCODE){throw 'DLL compilation failed'}
 & $compiler @common '-Ibuild/generated' '-municode' 'src/launcher.cpp' '-o' 'dist/BlindRage.exe' '-lbcrypt' '-luser32' '-lwinhttp' '-lole32' '-luuid'
 if($LASTEXITCODE){throw 'Launcher compilation failed'}
 & $compiler @common 'tests/reader_test.cpp' '-o' 'build/tests/reader_test.exe'
 if($LASTEXITCODE){throw 'Test compilation failed'}
 & $compiler @common '-municode' 'tests/speech_test.cpp' '-o' 'build/tests/speech_test.exe' '-lbcrypt' '-lole32' '-luser32'
 if($LASTEXITCODE){throw 'Speech test compilation failed'}
 Copy-Item -LiteralPath vendor/dynamic/release/bin/prism.dll -Destination dist/prism.dll
 # Keep manual speech diagnostics outside the installable distribution.
 Copy-Item -LiteralPath vendor/dynamic/release/bin/prism.dll -Destination build/tests/prism.dll
 New-Item -ItemType Directory -Force dist/licenses | Out-Null
 Copy-Item -LiteralPath vendor/NOTICE -Destination dist/licenses/Prism-NOTICE.txt
 Copy-Item -LiteralPath vendor/LICENSES -Destination dist/licenses -Recurse -Force
 Copy-Item -LiteralPath tools/llvm-mingw-20261117-ucrt-x86_64/LICENSE.TXT -Destination dist/licenses/LLVM-LICENSE.txt
 Copy-Item -LiteralPath "$hookRoot/LICENSE.txt" -Destination dist/licenses/MinHook-LICENSE.txt
 if($Test){
  & ./build/tests/hotkey_names_test.exe; if($LASTEXITCODE){throw 'Hotkey name tests failed'}
  & ./build/tests/settings_file_test.exe; if($LASTEXITCODE){throw 'Settings file tests failed'}
  & ./build/tests/mod_tutorials_test.exe; if($LASTEXITCODE){throw 'Mod tutorial tests failed'}
  & ./build/tests/fire_warning_test.exe; if($LASTEXITCODE){throw 'Fire warning tests failed'}
  & ./build/tests/recomp_setup_test.exe; if($LASTEXITCODE){throw 'Recomp setup tests failed'}
  & ./build/tests/shooting_beacons_test.exe; if($LASTEXITCODE){throw 'Shooting beacon tests failed'}
  & ./build/tests/reader_test.exe; if($LASTEXITCODE){throw 'Reader tests failed'}
  & ./build/hooks/hook_test.exe; if($LASTEXITCODE){throw 'Hook tests failed'}
  & ./build/hooks/ui_changes_test.exe; if($LASTEXITCODE){throw 'UI change tests failed'}
  & ./build/hooks/ui_hook_source_test.exe; if($LASTEXITCODE){throw 'UI hook source tests failed'}
  & ./build/speech/diagnostics_test.exe; if($LASTEXITCODE){throw 'Diagnostic tests failed'}
  & ./build/hooks/build_profile_test.exe; if($LASTEXITCODE){throw 'Build profile tests failed'}
  & ./build/hooks/gameplay_text_test.exe; if($LASTEXITCODE){throw 'Gameplay text tests failed'}
  & ./build/hooks/recomp_prompts_test.exe; if($LASTEXITCODE){throw 'Recomp prompt tests failed'}
  & ./build/hooks/gameplay_status_test.exe; if($LASTEXITCODE){throw 'Gameplay status tests failed'}
  & ./build/hooks/qte_reader_test.exe; if($LASTEXITCODE){throw 'QTE reader tests failed'}
  & ./build/hooks/synchronic_reader_test.exe; if($LASTEXITCODE){throw 'Synchronic reader tests failed'}
  & ./build/hooks/gameplay_hooks_test.exe; if($LASTEXITCODE){throw 'Gameplay hook tests failed'}
  & ./build/hooks/presentation_hooks_test.exe; if($LASTEXITCODE){throw 'Presentation hook tests failed'}
  & ./build/hooks/restart_test.exe; if($LASTEXITCODE){throw 'Restart tests failed'}
  & ./build/hooks/lock_trace_test.exe; if($LASTEXITCODE){throw 'Lock trace tests failed'}
 }
 if(!$NoInstall){
  if(!$InstallTo -and ((Test-Path -LiteralPath ../../bin_disc/asura_wrath_recomp.exe) -or (Test-Path -LiteralPath ../../bin/asura_wrath_recomp.exe))){$InstallTo=(Resolve-Path ../..).Path}
  if($InstallTo){& ./install.ps1 -GameRoot $InstallTo}
 }
} finally {Pop-Location}
