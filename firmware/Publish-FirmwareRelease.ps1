param(
    [Parameter(Mandatory = $true)]
    [string]$SourceBinary,

    [Parameter(Mandatory = $true)]
    [ValidatePattern('^V[0-9]+\.[0-9]+$')]
    [string]$Version,

    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[A-Za-z0-9][A-Za-z0-9-]*$')]
    [string]$ReleaseName,

    [string]$ReleaseDate = (Get-Date).ToString('yyyy-MM-dd')
)

$ErrorActionPreference = 'Stop'
$firmwareRoot = [System.IO.Path]::GetFullPath($PSScriptRoot)
$latestDirectory = Join-Path $firmwareRoot 'latest firmware'
$releasesDirectory = Join-Path $firmwareRoot 'firmware releases'
$sourcePath = (Resolve-Path -LiteralPath $SourceBinary -ErrorAction Stop).ProviderPath

if (-not $sourcePath.EndsWith('.ino.bin', [System.StringComparison]::OrdinalIgnoreCase)) {
    throw 'Only application .ino.bin files can be published as OTA firmware.'
}

$date = [datetime]::ParseExact(
    $ReleaseDate, 'yyyy-MM-dd',
    [System.Globalization.CultureInfo]::InvariantCulture)
$day = $date.ToString('yyyy-MM-dd')
$fileName = "$Version-$ReleaseName.ino.bin"
$archiveDirectory = Join-Path $releasesDirectory $day
$archivePath = Join-Path $archiveDirectory $fileName
$latestPath = Join-Path $latestDirectory $fileName
$sourceHash = (Get-FileHash -LiteralPath $sourcePath -Algorithm SHA256).Hash

New-Item -ItemType Directory -Path $releasesDirectory -Force | Out-Null
New-Item -ItemType Directory -Path $latestDirectory -Force | Out-Null

$sameVersion = @(Get-ChildItem -LiteralPath $releasesDirectory -Recurse -File -Filter "$Version-*.ino.bin")
foreach ($release in $sameVersion) {
    if ($release.FullName -ne $archivePath) {
        throw "$Version is already assigned to $($release.FullName)."
    }
}

New-Item -ItemType Directory -Path $archiveDirectory -Force | Out-Null
if (Test-Path -LiteralPath $archivePath) {
    if ((Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash -ne $sourceHash) {
        throw "The archived $Version file differs from this build."
    }
} else {
    Copy-Item -LiteralPath $sourcePath -Destination $archivePath -ErrorAction Stop
    if ((Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash -ne $sourceHash) {
        throw 'The archived copy did not match the built firmware.'
    }
}

$archiveHashes = @{}
Get-ChildItem -LiteralPath $releasesDirectory -Recurse -File -Filter '*.ino.bin' |
    ForEach-Object {
        $archiveHashes[(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash] = $true
    }

$previous = @(Get-ChildItem -LiteralPath $latestDirectory -File -Filter '*.ino.bin')
foreach ($firmware in $previous) {
    $previousHash = (Get-FileHash -LiteralPath $firmware.FullName -Algorithm SHA256).Hash
    if (-not $archiveHashes.ContainsKey($previousHash)) {
        throw "Previous latest firmware is not archived: $($firmware.FullName)"
    }
}

$pendingPath = Join-Path $latestDirectory "$fileName.pending"
if (Test-Path -LiteralPath $pendingPath) {
    throw "A pending release file already exists: $pendingPath"
}
Copy-Item -LiteralPath $archivePath -Destination $pendingPath -ErrorAction Stop
if ((Get-FileHash -LiteralPath $pendingPath -Algorithm SHA256).Hash -ne $sourceHash) {
    Remove-Item -LiteralPath $pendingPath
    throw 'The latest copy did not match the archive.'
}

foreach ($firmware in $previous) {
    Remove-Item -LiteralPath $firmware.FullName -ErrorAction Stop
}
Move-Item -LiteralPath $pendingPath -Destination $latestPath -ErrorAction Stop

Write-Output "Archive: $archivePath"
Write-Output "Latest:  $latestPath"
Write-Output "SHA256:  $sourceHash"
