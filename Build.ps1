[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$WindhawkPath,
    [string]$SigningScript,
    [switch]$RequireSignature
)
$ErrorActionPreference = 'Stop'
$source = [IO.File]::ReadAllText((Join-Path $PSScriptRoot 'src\taskbar-drop-pins.wh.cpp'))
$versionMatch = [regex]::Match($source, '(?m)^// @version\s+([0-9]+\.[0-9]+\.[0-9]+)\s*$')
if (-not $versionMatch.Success) { throw 'The mod source must declare a semantic @version.' }
$version = $versionMatch.Groups[1].Value
$compiler = Join-Path $WindhawkPath 'Compiler\bin\clang++.exe'
if (-not (Test-Path -LiteralPath $compiler)) { throw "Windhawk compiler was not found: $compiler" }
$engineRelative = ((Get-Content -LiteralPath (Join-Path $WindhawkPath 'windhawk.ini') |
    Where-Object { $_ -match '^EnginePath=' }) -replace '^EnginePath=', '').Trim()
$engineLibrary = Join-Path $WindhawkPath "$engineRelative\64\windhawk.lib"
$dist = Join-Path $PSScriptRoot 'dist'
New-Item -ItemType Directory -Path $dist -Force | Out-Null
$launcher = Join-Path $dist 'TaskbarDropPins.Launcher.exe'
$common = @('-std=c++23', '-O2', '-target', 'x86_64-w64-mingw32', '-DUNICODE', '-D_UNICODE',
    '-DWINVER=0x0A00', '-D_WIN32_WINNT=0x0A00', '-D_WIN32_IE=0x0A00',
    '-DNTDDI_VERSION=0x0A000008', '-D__USE_MINGW_ANSI_STDIO=0', '-Wall', '-Wextra', '-Werror')
& $compiler @common '-static' '-municode' '-mwindows' '-s' (Join-Path $PSScriptRoot 'src\launcher.cpp') '-o' $launcher '-lshell32' '-lole32'
if ($LASTEXITCODE) { throw "Launcher compilation failed with exit code $LASTEXITCODE" }
if ($SigningScript) {
    if (-not (Test-Path -LiteralPath $SigningScript -PathType Leaf)) { throw "Signing script was not found: $SigningScript" }
    & $SigningScript $launcher
    if ($LASTEXITCODE) { throw 'Launcher signing failed.' }
}
if ($SigningScript -or $RequireSignature) {
    $signature = Get-AuthenticodeSignature -LiteralPath $launcher
    if ($signature.Status -ne 'Valid') { throw "Launcher signature is $($signature.Status)." }
}
$bytes = [IO.File]::ReadAllBytes($launcher)
$rows = for ($offset=0; $offset -lt $bytes.Length; $offset+=24) {
    $last = [Math]::Min($offset+23, $bytes.Length-1)
    '    ' + (($bytes[$offset..$last] | ForEach-Object { '0x{0:x2}' -f $_ }) -join ',') + ','
}
$embedded = "static const unsigned char kLauncherBytes[] = {`n" + ($rows -join "`n") + "`n};"
$bundled = $source.Replace('#include "launcher-bytes.h"', $embedded)
$shellPins = [IO.File]::ReadAllText((Join-Path $PSScriptRoot 'src\shell-pins.h'))
$bundled = $bundled.Replace('#include "shell-pins.h"', $shellPins)
$sourceDrop = [IO.File]::ReadAllText((Join-Path $PSScriptRoot 'src\source-drop.h'))
$bundled = $bundled.Replace('#include "source-drop.h"', $sourceDrop)
$modSource = Join-Path $dist 'taskbar-drop-pins.wh.cpp'
[IO.File]::WriteAllText($modSource, $bundled, [Text.UTF8Encoding]::new($false))
$modLibrary = Join-Path $dist 'taskbar-drop-pins.dll'
$versionDefine = '-DWH_MOD_VERSION=L"' + $version + '"'
& $compiler @common '-shared' '-DWH_MOD' '-DWH_MOD_ID=L"taskbar-drop-pins"' $versionDefine `
    $engineLibrary '-x' 'c++' $modSource '-include' 'windhawk_api.h' '-Wl,--export-all-symbols' `
    '-o' $modLibrary '-lole32' '-loleaut32' '-lshell32' '-lshlwapi' '-lpropsys' '-luuid' '-lcomctl32' '-lruntimeobject' '-luiautomationcore' '-lwindowscodecs'
if ($LASTEXITCODE) { throw "Mod compilation failed with exit code $LASTEXITCODE" }
Get-Item -LiteralPath $launcher, $modLibrary, $modSource | Select-Object Name, Length, FullName
