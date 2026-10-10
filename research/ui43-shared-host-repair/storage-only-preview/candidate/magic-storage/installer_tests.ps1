param(
    [string]$FixtureRoot='E:\Build\enshrouded-flight-menu-20261010\storage-source\magic-storage\full-preview-fixtures',
    [string]$GameExecutable='D:\Games\SteamLibrary\steamapps\common\Enshrouded\enshrouded.exe',
    [string]$BuildDirectory='E:\Build\enshrouded-flight-menu-20261010\storage-build',
    [string]$CreativeBuildDirectory='E:\Build\enshrouded-flight-menu-20261010\package-build'
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$fixture=Join-Path $FixtureRoot ([guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $fixture | Out-Null
$script:checks=0
function Require([bool]$Value,[string]$Message) {
    ++$script:checks
    if(!$Value){throw $Message}
}
# Mock only the OS process provider. The production installer has no bypass.
function Get-CimInstance {param($ClassName,$Filter,$ErrorAction) [pscustomobject]@{Name='enshrouded.exe'}}
$installer=Join-Path $PSScriptRoot 'Install-UiPreview.ps1'
$configPath=Join-Path $fixture 'shroudtopia.json'
Copy-Item -LiteralPath $GameExecutable -Destination (Join-Path $fixture 'enshrouded.exe')
New-Item -ItemType Directory -Path (Join-Path $fixture 'mods\creative_mode'),(Join-Path $fixture 'mods\magic_storage_probe') | Out-Null
[IO.File]::WriteAllText((Join-Path $fixture 'mods\creative_mode\creative_mode.dll'),'original Creative fixture')
[IO.File]::WriteAllText((Join-Path $fixture 'mods\creative_mode\mod.json'),'{"id":"Creative Mode","version":"fixture"}')
[IO.File]::WriteAllText((Join-Path $fixture 'mods\creative_mode\catalogue.tsv'),'original catalogue fixture')
[IO.File]::WriteAllText((Join-Path $fixture 'mods\magic_storage_probe\magic_storage_probe.dll'),'original probe fixture')
[IO.File]::WriteAllText((Join-Path $fixture 'mods\magic_storage_probe\observations.tsv'),'retained observations')
$original='{"other":"preserve","mods":{"MagicStorageProbe":{"active":true,"custom":"keep"},"Creative Mode":{"active":true,"menu_key":118,"flight_key":71}}}'
[IO.File]::WriteAllText($configPath,$original)
$refused=$false
try {& $installer -GameDirectory $fixture -BuildDirectory $BuildDirectory -CreativeBuildDirectory $CreativeBuildDirectory -Confirm:$false} catch {$refused=$_.Exception.Message -like 'Close Enshrouded*';if(!$refused){throw}}
Require $refused 'Running game must block installation'
Require ([IO.File]::ReadAllText($configPath) -eq $original) 'Refused install changed configuration'
function Get-CimInstance {param($ClassName,$Filter,$ErrorAction) @()}
& $installer -GameDirectory $fixture -BuildDirectory $BuildDirectory -CreativeBuildDirectory $CreativeBuildDirectory -WhatIf
Require (!(Test-Path -LiteralPath (Join-Path $fixture 'mods\magic_storage'))) 'WhatIf changed files'
$managerState=Join-Path $fixture '.EnshroudedClientMods'
New-Item -ItemType Directory -Path $managerState | Out-Null
$managerLock=Join-Path $managerState 'operation.lock'
[IO.File]::WriteAllBytes($managerLock,[byte[]]@())
$gate=[IO.File]::Open($managerLock,[IO.FileMode]::Open,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
$refused=$false
try {& $installer -GameDirectory $fixture -BuildDirectory $BuildDirectory -CreativeBuildDirectory $CreativeBuildDirectory -Confirm:$false} catch {$refused=$_.Exception.Message -like 'The client manager transaction lock*'} finally {$gate.Dispose()}
Require $refused 'Active manager transaction must block installation'
Require ([IO.File]::ReadAllText($configPath) -eq $original -and !(Test-Path -LiteralPath (Join-Path $fixture 'mods\magic_storage'))) 'Manager-lock refusal changed installation'
$locked=[IO.File]::Open($configPath,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)
$refused=$false
try {& $installer -GameDirectory $fixture -BuildDirectory $BuildDirectory -CreativeBuildDirectory $CreativeBuildDirectory -Confirm:$false} catch {$refused=$true} finally {$locked.Dispose()}
Require $refused 'Locked config must prevent committing an installation'
Require (!(Test-Path -LiteralPath (Join-Path $fixture 'mods\magic_storage')) -and
         [IO.File]::ReadAllText((Join-Path $fixture 'mods\creative_mode\creative_mode.dll')) -eq 'original Creative fixture') 'Failed commit did not roll back files'
Require ([IO.File]::ReadAllText($configPath) -eq $original) 'Failed commit changed configuration'
$processCounter=[pscustomobject]@{Value=0}
function Get-CimInstance {
    param($ClassName,$Filter,$ErrorAction)
    if(++$processCounter.Value -eq 3) {
        [IO.File]::WriteAllText((Join-Path $fixture 'mods\creative_mode\catalogue.tsv'),'concurrent catalogue change')
    }
    @()
}
$refused=$false
try {& $installer -GameDirectory $fixture -BuildDirectory $BuildDirectory -CreativeBuildDirectory $CreativeBuildDirectory -Confirm:$false} catch {$refused=$_.Exception.Message -like 'Mod files changed during staging*';if(!$refused){throw}}
Require $refused 'Concurrent mod change must block installation'
Require (!(Test-Path -LiteralPath (Join-Path $fixture 'mods\magic_storage')) -and
         [IO.File]::ReadAllText((Join-Path $fixture 'mods\creative_mode\catalogue.tsv')) -eq 'concurrent catalogue change') 'Concurrent mod change was overwritten'
Require ([IO.File]::ReadAllText($configPath) -eq $original) 'Concurrent mod change altered configuration'
function Get-CimInstance {param($ClassName,$Filter,$ErrorAction) @()}
& $installer -GameDirectory $fixture -BuildDirectory $BuildDirectory -CreativeBuildDirectory $CreativeBuildDirectory -Confirm:$false
$config=Get-Content -LiteralPath $configPath -Raw | ConvertFrom-Json
Require ($config.mods.MagicStorage.active -and $config.mods.MagicStorage.menu_key -eq 0 -and !$config.mods.MagicStorageProbe.active) 'Preview config was not installed'
Require ($config.mods.MagicStorageProbe.custom -eq 'keep' -and $config.mods.'Creative Mode'.menu_key -eq 118) 'Existing settings changed'
Require (!(Test-Path -LiteralPath (Join-Path $fixture 'mods\magic_storage_probe'))) 'Old probe would conflict with preview hook'
Require (Test-Path -LiteralPath (Join-Path $fixture 'native_ui_host\native_ui_host.dll')) 'Shared host missing'
Require (!(Test-Path -LiteralPath (Join-Path $fixture 'mods\native_ui_host'))) 'Non-mod helper must stay outside the EML scan root'
$manifestless=@(Get-ChildItem -LiteralPath (Join-Path $fixture 'mods') -Directory | Where-Object {!(Test-Path -LiteralPath (Join-Path $_.FullName 'mod.json'))})
Require ($manifestless.Count -eq 0) 'Every installed direct mods directory needs an EML manifest'
$hostDll=Join-Path $fixture 'native_ui_host\native_ui_host.dll'
$hostBytes=[IO.File]::ReadAllBytes($hostDll)
[IO.File]::WriteAllText($hostDll,'newer external host');$refused=$false
try {& $installer -Action Remove -GameDirectory $fixture -Confirm:$false} catch {$refused=$_.Exception.Message -like '*files changed*'}
Require $refused 'Removal overwrote a changed host'
[IO.File]::WriteAllBytes($hostDll,$hostBytes)
[IO.File]::WriteAllText((Join-Path $fixture 'mods\magic_storage\magic-storage-probe.tsv'),'preview observations')
$config.other='unrelated user change';$config | ConvertTo-Json -Depth 100 | Set-Content -LiteralPath $configPath
& $installer -Action Remove -GameDirectory $fixture -Confirm:$false
$restored=Get-Content -LiteralPath $configPath -Raw | ConvertFrom-Json
Require ($restored.other -eq 'unrelated user change') 'Removal lost unrelated configuration'
Require ($restored.mods.MagicStorageProbe.active -and $restored.mods.MagicStorageProbe.custom -eq 'keep') 'Original probe settings not restored'
Require (!$restored.mods.PSObject.Properties['MagicStorage']) 'Preview configuration not removed'
Require ([IO.File]::ReadAllText((Join-Path $fixture 'mods\creative_mode\creative_mode.dll')) -eq 'original Creative fixture') 'Original Creative not restored'
Require ([IO.File]::ReadAllText((Join-Path $fixture 'mods\magic_storage_probe\observations.tsv')) -eq 'retained observations') 'Probe observations lost'
Require (!(Test-Path -LiteralPath (Join-Path $fixture 'native_ui_host'))) 'New host not removed'
$reports=@(Get-ChildItem -LiteralPath (Join-Path $fixture '.magic-storage-ui-backups') -Filter 'magic-storage-probe.tsv' -Recurse)
Require ($reports.Count -eq 1 -and [IO.File]::ReadAllText($reports[0].FullName) -eq 'preview observations') 'Preview observations not retained'
# A second clean install/remove must restore the original bytes exactly.
$before=[IO.File]::ReadAllBytes($configPath)
& $installer -GameDirectory $fixture -BuildDirectory $BuildDirectory -CreativeBuildDirectory $CreativeBuildDirectory -Confirm:$false
& $installer -Action Remove -GameDirectory $fixture -Confirm:$false
Require ([Convert]::ToBase64String([IO.File]::ReadAllBytes($configPath)) -eq [Convert]::ToBase64String($before)) 'Unchanged config was not restored byte-for-byte'
Write-Output "$script:checks installer behavior checks passed. Fixture retained: $fixture"
