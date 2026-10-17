[CmdletBinding()]
param([Parameter(Mandatory)][string]$TrustDirectory)
$ErrorActionPreference = 'Stop'
$dist = Join-Path $PSScriptRoot 'dist'
$source = Join-Path $dist 'taskbar-drop-pins.wh.cpp'
$launcher = Join-Path $dist 'TaskbarDropPins.Launcher.exe'
if (-not (Test-Path -LiteralPath $source) -or -not (Test-Path -LiteralPath $launcher)) {
    throw 'Run Build.ps1 with signing before packaging.'
}
if ((Get-AuthenticodeSignature -LiteralPath $launcher).Status -ne 'Valid') {
    throw 'The release launcher must have a valid Authenticode signature.'
}
$content = [IO.File]::ReadAllText($source)
$versionMatch = [regex]::Match($content, '(?m)^// @version\s+([0-9]+\.[0-9]+\.[0-9]+)\s*$')
if (-not $versionMatch.Success) { throw 'Missing mod @version.' }
$version = $versionMatch.Groups[1].Value
$array = [regex]::Match($content, '(?s)static const unsigned char kLauncherBytes\[\] = \{(.*?)\};')
if (-not $array.Success) { throw 'The mod source does not embed the launcher.' }
[byte[]]$embedded = [regex]::Matches($array.Groups[1].Value, '0x([0-9a-fA-F]{2})') |
    ForEach-Object { [Convert]::ToByte($_.Groups[1].Value, 16) }
if ([Convert]::ToHexString($embedded) -ne [Convert]::ToHexString([IO.File]::ReadAllBytes($launcher))) {
    throw 'The embedded launcher differs from the signed release launcher.'
}
$output = Join-Path $PSScriptRoot "publish\v$version"
New-Item -ItemType Directory -Path $output -Force | Out-Null
$modOutput = Join-Path $output 'taskbar-drop-pins.wh.cpp'
Copy-Item -LiteralPath $source -Destination $modOutput

# Only explicit release files enter archives. No runtime data or signing keys.
function Write-ReleaseArchive([string]$ArchivePath, [hashtable]$Entries) {
    $stream = [IO.File]::Open($ArchivePath, [IO.FileMode]::Create, [IO.FileAccess]::Write)
    try {
        $archive = [IO.Compression.ZipArchive]::new($stream, [IO.Compression.ZipArchiveMode]::Create, $true)
        try {
            foreach ($entry in $Entries.GetEnumerator() | Sort-Object Key) {
                [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, $entry.Value,
                    $entry.Key, [IO.Compression.CompressionLevel]::Optimal) | Out-Null
            }
        } finally { $archive.Dispose() }
    } finally { $stream.Dispose() }
}
$package = Join-Path $output "TaskbarDropPins-v$version.zip"
Write-ReleaseArchive $package @{
    'TaskbarDropPins/taskbar-drop-pins.wh.cpp' = $source
    'TaskbarDropPins/README.md' = (Join-Path $PSScriptRoot 'README.md')
    'TaskbarDropPins/LICENSE' = (Join-Path $PSScriptRoot 'LICENSE')
}
$trust = Join-Path $output 'SignatureTrust.zip'
Write-ReleaseArchive $trust @{
    'Install-Certificate.cmd' = (Join-Path $TrustDirectory 'Install-Certificate.cmd')
    'README.txt' = (Join-Path $TrustDirectory 'README.txt')
    'RootCertificate.cer' = (Join-Path $TrustDirectory 'RootCertificate.cer')
}
$checksums = foreach ($path in @($modOutput, $package, $trust)) {
    (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() + '  ' + [IO.Path]::GetFileName($path)
}
[IO.File]::WriteAllLines((Join-Path $output 'SHA256SUMS.txt'), $checksums, [Text.UTF8Encoding]::new($false))
Get-ChildItem -LiteralPath $output -File | Select-Object Name, Length, FullName
