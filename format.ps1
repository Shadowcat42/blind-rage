param([switch]$Check)
$ErrorActionPreference = 'Stop'
$formatter = Join-Path $PSScriptRoot 'tools/llvm-mingw-20261117-ucrt-x86_64/bin/clang-format.exe'
if (!(Test-Path -LiteralPath $formatter)) {
    throw 'Pinned clang-format missing; run bootstrap.ps1 first.'
}
# Only original C++ code. Dependencies, captures and generated game code are excluded.
$files = @(
    foreach ($directory in @('src', 'tests')) {
        Get-ChildItem -LiteralPath (Join-Path $PSScriptRoot $directory) -Recurse -File |
            Where-Object { $_.Extension -in @('.cpp', '.h') } |
            Sort-Object FullName |
            ForEach-Object { $_.FullName }
    }
)
$options = @('--style=file')
if ($Check) {
    $options += @('--dry-run', '--Werror')
} else {
    $options += '-i'
}
& $formatter @options @files
if ($LASTEXITCODE) {
    throw 'C++ formatting failed. Run ./format.ps1 to apply the project style.'
}
Write-Output $(if ($Check) { 'C++ formatting check passed.' } else { 'C++ formatting applied.' })
