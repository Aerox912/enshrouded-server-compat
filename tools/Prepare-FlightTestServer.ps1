param(
    [string]$Baseline = 'E:\Scratch\enshrouded-vein-mining-20261005\local-server',
    [string]$Destination = 'E:\Scratch\enshrouded-flight-runtime-20261008',
    [string]$Build = 'E:\Build\enshrouded-flight-server\Release'
)
$ErrorActionPreference = 'Stop'
$testPath = [IO.Path]::GetFullPath($Destination)
if (-not $testPath.StartsWith('E:\Scratch\enshrouded-flight-', [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Test directory must be inside the named E:\Scratch flight area.'
}
if (Test-Path -LiteralPath $testPath) { throw 'Test directory already exists. Refusing to replace it.' }
$expected = '001C1B40ED091D8C1AEE583ADDE3800D7C858AE2C7F4DFF54FCA2938B2BE1637'
if ((Get-FileHash -LiteralPath (Join-Path $Baseline 'enshrouded_server.exe')).Hash -ne $expected) {
    throw 'Baseline executable is not the verified revision.'
}
New-Item -ItemType Directory -Path $testPath | Out-Null
# Immutable resource archives are shared read-only by the server. Config,
# loader, logs, plugins and worlds are independent copies in the test directory.
Get-ChildItem -LiteralPath $Baseline -File | Where-Object {
    $_.Extension -eq '.dat' -or $_.Name -in @('enshrouded_server.kfc','enshrouded_server.kfc_resources')
} | ForEach-Object {
    New-Item -ItemType HardLink -Path (Join-Path $testPath $_.Name) -Target $_.FullName | Out-Null
}
foreach ($name in @('enshrouded_server.exe','steam_api64.dll','steamclient64.dll','tier0_s64.dll','vstdlib_s64.dll','steam_appid.txt','GlobalXPShare.original.dll')) {
    Copy-Item -LiteralPath (Join-Path $Baseline $name) -Destination (Join-Path $testPath $name)
}
Copy-Item -LiteralPath (Join-Path $Baseline 'mods') -Destination (Join-Path $testPath 'mods') -Recurse
Copy-Item -LiteralPath (Join-Path $Build 'flight_server_dev.dll') -Destination (Join-Path $testPath 'dbghelp.dll')
$settings = Get-Content -LiteralPath (Join-Path $Baseline 'enshrouded_server.json') -Raw | ConvertFrom-Json
$settings.name = 'Flight verification (local)'
$settings.saveDirectory = './test-world'
$settings.logDirectory = './logs'
$settings.ip = '127.0.0.1'
$settings.queryPort = 15637
$settings.slotCount = 3
foreach ($group in $settings.userGroups) { $group.password = '' }
$settings | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath (Join-Path $testPath 'enshrouded_server.json') -Encoding utf8NoBOM
# Deliberately no approved IDs in this reusable script. Supply a private
# flight-allowlist.cfg separately; missing config denies all flight.
Write-Output "Prepared independent local test server at $testPath"
