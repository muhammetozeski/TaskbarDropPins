[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$WindhawkPath,
    [switch]$Disable
)
$ErrorActionPreference = 'Stop'
$modId = 'taskbar-drop-pins'
$ini = Get-Content -LiteralPath (Join-Path $WindhawkPath 'windhawk.ini')
if (-not ($ini -contains 'Portable=1')) { throw 'This installer supports portable Windhawk.' }
$appDataRelative = (($ini | Where-Object { $_ -match '^AppDataPath=' }) -replace '^AppDataPath=', '').Trim()
$appData = Join-Path $WindhawkPath $appDataRelative
$mods = Join-Path $appData 'Engine\Mods'
$configFile = Join-Path $mods "$modId.ini"
if ($Disable) {
    if (-not (Test-Path -LiteralPath $configFile)) { throw 'The mod is not installed.' }
    $content = [IO.File]::ReadAllText($configFile) -replace '(?m)^Disabled=\d+', 'Disabled=1'
    [IO.File]::WriteAllText($configFile, $content, [Text.UTF8Encoding]::new($false))
    Write-Output 'Taskbar Drop Pins disabled.'
    return
}
$library = Join-Path $PSScriptRoot 'dist\taskbar-drop-pins.dll'
$source = Join-Path $PSScriptRoot 'dist\taskbar-drop-pins.wh.cpp'
if (-not (Test-Path -LiteralPath $library) -or -not (Test-Path -LiteralPath $source)) {
    throw 'Build.ps1 must finish before installation.'
}
$versionMatch = [regex]::Match([IO.File]::ReadAllText($source), '(?m)^// @version\s+([0-9]+\.[0-9]+\.[0-9]+)\s*$')
if (-not $versionMatch.Success) { throw 'The built source does not declare a semantic @version.' }
$version = $versionMatch.Groups[1].Value
$libraryName = "taskbar-drop-pins_${version}_" + (Get-FileHash -LiteralPath $library -Algorithm SHA256).Hash.Substring(0,12).ToLowerInvariant() + '.dll'
New-Item -ItemType Directory -Path (Join-Path $mods '64') -Force | Out-Null
$installedLibrary = Join-Path $mods "64\$libraryName"
if (-not (Test-Path -LiteralPath $installedLibrary)) { Copy-Item -LiteralPath $library -Destination $installedLibrary }
Copy-Item -LiteralPath $source -Destination (Join-Path $appData "ModsSource\$modId.wh.cpp")
$configuration = @"
[Mod]
LibraryFileName=$libraryName
Disabled=0
LoggingEnabled=0
DebugLoggingEnabled=0
Include=explorer.exe
Exclude=
Architecture=x86-64
Version=$version
SettingsChangeTime=$([DateTimeOffset]::UtcNow.ToUnixTimeSeconds())
"@
[IO.File]::WriteAllText($configFile, $configuration, [Text.UTF8Encoding]::new($false))
Write-Output "Installed: $installedLibrary"
Write-Output "Configuration: $configFile"
