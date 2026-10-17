[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$TestDirectory,
    [Parameter(Mandatory)][string]$WindhawkPath
)
$ErrorActionPreference = 'Stop'
if (-not (Test-Path -LiteralPath $TestDirectory -PathType Container)) { throw 'Pass an existing empty NewTemp directory.' }
if (Get-ChildItem -LiteralPath $TestDirectory -Force) { throw 'TestDirectory must be empty.' }
$compiler = Join-Path $WindhawkPath 'Compiler\bin\clang++.exe'
$output = Join-Path $TestDirectory 'shell-contract.exe'
& $compiler '-std=c++23' '-O2' '-target' 'x86_64-w64-mingw32' '-DUNICODE' '-D_UNICODE' `
    '-DWH_MOD' '-D_WIN32_WINNT=0x0A00' '-municode' '-static' `
    (Join-Path $PSScriptRoot 'tests\shell-contract.cpp') '-o' $output `
    '-lole32' '-loleaut32' '-lshell32' '-lshlwapi' '-lpropsys' '-luuid' '-lcomctl32' '-lruntimeobject' '-luiautomationcore' '-lwindowscodecs'
if ($LASTEXITCODE) { throw "Test compilation failed with exit code $LASTEXITCODE" }
& $output $TestDirectory
if ($LASTEXITCODE) { throw "Shell contract test failed with exit code $LASTEXITCODE" }
