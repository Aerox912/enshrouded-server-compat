[CmdletBinding()]
param(
    [string]$TestRoot='E:\Build\enshrouded-flight-menu-20261010\storage-source\magic-storage\test-runs',
    [string]$GameExecutable='D:\Games\SteamLibrary\steamapps\common\Enshrouded\enshrouded.exe',
    [string]$StorageBuild='E:\Build\enshrouded-flight-menu-20261010\storage-build',
    [string]$CreativeDll='E:\Build\enshrouded-flight-menu-20261010\package-build\Release\creative_client_dev.dll',
    [string]$HostDll='E:\Build\enshrouded-flight-menu-20261010\storage-build\ui-host\Release\native_ui_host.dll'
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$script:Installer=(Join-Path $PSScriptRoot 'Install-UiPreview.ps1')
$script:StorageBuild=$StorageBuild
$script:CreativeDll=$CreativeDll
$script:HostDll=$HostDll
$global:Storage167MoveCall=0
$global:Storage167MoveFaultCalls=@()
function Require([bool]$Condition,[string]$Message) {
    if(!$Condition){throw $Message}
}
function Get-Hash([string]$Path) {
    (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}
function Write-Json([string]$Path,$Value) {
    [IO.File]::WriteAllText($Path,(ConvertTo-Json -InputObject $Value -Depth 100)+[Environment]::NewLine,[Text.UTF8Encoding]::new($false))
}
function Get-ManagerSnapshot([string]$Root) {
    [pscustomobject]@{
        receipt=(Get-Hash (Join-Path $Root '.EnshroudedClientMods\active.json'))
        creative=(Get-Hash (Join-Path $Root 'mods\creative_mode\creative_mode.dll'))
        host=(Get-Hash (Join-Path $Root 'native_ui_host\native_ui_host.dll'))
    }
}
function Assert-ManagerSnapshot([string]$Root,$Expected,[string]$CaseName) {
    $current=Get-ManagerSnapshot $Root
    Require ($current.receipt -ceq $Expected.receipt) "$CaseName changed the manager receipt."
    Require ($current.creative -ceq $Expected.creative) "$CaseName changed manager-owned Creative."
    Require ($current.host -ceq $Expected.host) "$CaseName changed the manager-owned host."
}
function Get-Config([string]$Root) {
    Get-Content -LiteralPath (Join-Path $Root 'shroudtopia.json') -Raw | ConvertFrom-Json
}
function Reset-Fixture {
    foreach($relative in @('mods','.EnshroudedClientMods','.magic-storage-ui-backups','native_ui_host')) {
        $path=Join-Path $script:Fixture $relative
        $full=[IO.Path]::GetFullPath($path)
        if(!$full.StartsWith($script:Fixture+'\',[StringComparison]::OrdinalIgnoreCase)) {
            throw "Unsafe fixture reset path: $relative"
        }
        if(Test-Path -LiteralPath $full) {Remove-Item -LiteralPath $full -Recurse -Force}
    }
    $configPath=Join-Path $script:Fixture 'shroudtopia.json'
    if(Test-Path -LiteralPath $configPath){Remove-Item -LiteralPath $configPath -Force}
    New-Item -ItemType Directory -Path (Join-Path $script:Fixture 'mods\creative_mode'),(Join-Path $script:Fixture 'native_ui_host') -Force | Out-Null
    $creativePath=Join-Path $script:Fixture 'mods\creative_mode\creative_mode.dll'
    [IO.File]::WriteAllText($creativePath,'pre-manager Creative baseline',[Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText((Join-Path $script:Fixture 'mods\creative_mode\mod.json'),'{"id":"Creative Mode","version":"fixture"}',[Text.UTF8Encoding]::new($false))
    $creativeBefore=Get-Hash $creativePath
    $managerState=Join-Path $script:Fixture '.EnshroudedClientMods'
    $managerBackup=Join-Path $managerState 'backups\20261010-000000-a1b2c3'
    $creativeBaseline=Join-Path $managerBackup 'originals\mods\creative_mode\creative_mode.dll'
    New-Item -ItemType Directory -Path (Split-Path -Parent $creativeBaseline) -Force | Out-Null
    Copy-Item -LiteralPath $creativePath -Destination $creativeBaseline
    Copy-Item -LiteralPath $script:CreativeDll -Destination $creativePath -Force
    Copy-Item -LiteralPath $script:HostDll -Destination (Join-Path $script:Fixture 'native_ui_host\native_ui_host.dll') -Force
    $creativeAfter=Get-Hash $creativePath
    $hostAfter=Get-Hash (Join-Path $script:Fixture 'native_ui_host\native_ui_host.dll')
    $receipt=[pscustomobject]@{
        PackageId='enshrouded-client-mods-more-cheese-v1'
        Root=$script:Fixture
        Backup='backups\20261010-000000-a1b2c3'
        Status='Installed'
        FirstPerson=$false
        Selection=[pscustomobject]@{CreativeMode=$true}
        Files=@(
            [pscustomobject]@{Path='mods\creative_mode\creative_mode.dll';Before=$creativeBefore;After=$creativeAfter},
            [pscustomobject]@{Path='native_ui_host\native_ui_host.dll';Before=$null;After=$hostAfter}
        )
    }
    New-Item -ItemType Directory -Path $managerState -Force | Out-Null
    [IO.File]::WriteAllBytes((Join-Path $managerState 'operation.lock'),[byte[]]@())
    Write-Json (Join-Path $managerState 'active.json') $receipt
    $mods=[ordered]@{
        MagicStorageProbe=[pscustomobject]@{active=$true;custom='keep'}
        'Creative Mode'=[pscustomobject]@{active=$true;menu_key=118;flight_key=71}
    }
    Write-Json $configPath ([pscustomobject]@{other='preserve';mods=[pscustomobject]$mods})
    return Get-ManagerSnapshot $script:Fixture
}
function Move-Item {
    param($LiteralPath,$Destination,$ErrorAction)
    $global:Storage167MoveCall++
    if($global:Storage167MoveFaultCalls -contains $global:Storage167MoveCall) {
        $failedCall=$global:Storage167MoveCall
        $global:Storage167MoveFaultCalls=@($global:Storage167MoveFaultCalls | Where-Object {$_ -ne $failedCall})
        throw "Injected Storage-only Move-Item failure at call $failedCall."
    }
    Microsoft.PowerShell.Management\Move-Item -LiteralPath $LiteralPath -Destination $Destination -ErrorAction Stop
}
function Invoke-Storage([ValidateSet('Install','Remove')][string]$Action,[int[]]$FaultCalls=@(),[string]$BuildDirectory=$script:StorageBuild) {
    $global:Storage167MoveCall=0
    $global:Storage167MoveFaultCalls=@($FaultCalls)
    try {
        if($Action -eq 'Install') {
            $output=@(& $script:Installer -Action Install -Mode StorageOnly -GameDirectory $script:Fixture -BuildDirectory $BuildDirectory -Confirm:$false -ErrorAction Stop)
        } else {
            $output=@(& $script:Installer -Action Remove -GameDirectory $script:Fixture -Confirm:$false -ErrorAction Stop)
        }
        return [pscustomobject]@{succeeded=$true;output=([string]::Join([Environment]::NewLine,@($output)));error=$null;moveCalls=$global:Storage167MoveCall}
    } catch {
        return [pscustomobject]@{succeeded=$false;output=$null;error=$_.Exception.Message;moveCalls=$global:Storage167MoveCall}
    } finally {$global:Storage167MoveFaultCalls=@()}
}
function Invoke-StorageWithConfigLock([ValidateSet('Install','Remove')][string]$Action) {
    $stream=$null
    try {
        $stream=[IO.File]::Open((Join-Path $script:Fixture 'shroudtopia.json'),[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)
        return Invoke-Storage $Action
    } finally {if($stream){$stream.Dispose()}}
}
function Attach-Storage([string]$CaseName) {
    $result=Invoke-Storage Install
    Require $result.succeeded "$CaseName attach failed: $($result.error)"
    $receipt=Get-Content -LiteralPath (Join-Path $script:Fixture 'mods\magic_storage\install-receipt.json') -Raw | ConvertFrom-Json
    Require ($receipt.schema -eq 3 -and $receipt.mode -ceq 'StorageOnly') "$CaseName did not create a Storage-only receipt."
Require ($receipt.ownership.storageDirectory -ceq 'mods\magic_storage' -and
         (@($receipt.ownership.configKeys) -join '|') -ceq 'MagicStorage|MagicStorageProbe' -and
         (@($receipt.ownership.managerReadOnly) -join '|') -ceq 'mods\creative_mode\creative_mode.dll|native_ui_host\native_ui_host.dll') "$CaseName receipt did not record exact Storage/config ownership and read-only manager paths."
Require ($receipt.storageManifestSha256 -ceq '16c496b58f799becd074145f174451b8c2cba9e20eeb71190e48d8ce8a325f8f' -and
         $receipt.installed.magic_storage.'mod.json' -ceq $receipt.storageManifestSha256) "$CaseName receipt did not pin the owned manifest."
    return $receipt
}
function Remove-Storage([string]$CaseName) {
    $result=Invoke-Storage Remove
    Require $result.succeeded "$CaseName remove failed: $($result.error)"
    return $result
}
function Add-Result([string]$Name,[string]$Outcome) {
    $script:Results.Add([pscustomobject]@{name=$Name;outcome=$Outcome})
}

foreach($path in @($script:Installer,$GameExecutable,$StorageBuild,$CreativeDll,$HostDll)) {
    if(!(Test-Path -LiteralPath $path)){throw "Required APP167 test input is missing: $path"}
}
Require ((Get-Hash (Join-Path $StorageBuild 'Release\magic_storage_ui_preview.dll')) -ceq '403d7b85ab595888da4840e3fb5f07451fb98c0bf8883864a218c01c778ffcab') 'Storage test binary does not match the accepted pin.'
Require ((Get-Hash $CreativeDll) -ceq 'befee002983183009129efb76aff80dcb017f5933ea6da6b05aaa7d652ab6250') 'Creative test input does not match the accepted pin.'
Require ((Get-Hash $HostDll) -ceq 'b6d9e4757d4305bd9aba4325ebd31c3c4e2a31ac6931999f327fe3dbd22f6b46') 'Host test input does not match the accepted pin.'
$testRoot=[IO.Path]::GetFullPath($TestRoot)
New-Item -ItemType Directory -Path $testRoot -Force | Out-Null
$script:RunRoot=Join-Path $testRoot (([DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ'))+'-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $script:RunRoot | Out-Null
$script:Fixture=Join-Path $script:RunRoot 'game-fixture'
New-Item -ItemType Directory -Path $script:Fixture | Out-Null
Copy-Item -LiteralPath $GameExecutable -Destination (Join-Path $script:Fixture 'enshrouded.exe')
$script:Results=[Collections.Generic.List[object]]::new()
function Get-CimInstance {param($ClassName,$Filter,$ErrorAction) @()}

$baseline=Reset-Fixture
$configBefore=Get-Hash (Join-Path $script:Fixture 'shroudtopia.json')
$receipt=Attach-Storage 'success'
Assert-ManagerSnapshot $script:Fixture $baseline 'Successful attach'
$installedConfig=Get-Config $script:Fixture
Require ($installedConfig.mods.MagicStorage.active -and $installedConfig.mods.MagicStorage.menu_key -eq 0) 'Storage-only attach configuration was not enabled.'
Require (!$installedConfig.mods.MagicStorageProbe.active -and $installedConfig.mods.MagicStorageProbe.custom -ceq 'keep') 'Probe setting was not disabled while preserving its other fields.'
Require ($installedConfig.mods.'Creative Mode'.menu_key -eq 118 -and $installedConfig.other -ceq 'preserve') 'Storage-only attach changed unrelated configuration.'
Require (!(Test-Path -LiteralPath (Join-Path $receipt.backup 'originals\mods')) -and !(Test-Path -LiteralPath (Join-Path $receipt.backup 'originals\native_ui_host'))) 'Storage-only attach backed up manager-owned mod directories.'
Add-Result 'attach-manager-owned-pair' 'passed'
$installedConfig.other='PRESERVE';Write-Json (Join-Path $script:Fixture 'shroudtopia.json') $installedConfig
$retainedPath=Join-Path $receipt.backup 'removed\magic_storage'
Remove-Storage 'success'
Assert-ManagerSnapshot $script:Fixture $baseline 'Successful remove'
$removedConfig=Get-Config $script:Fixture
Require ($removedConfig.other -ceq 'PRESERVE') 'Removal lost a case-only unrelated user configuration change.'
Require (!$removedConfig.mods.PSObject.Properties['MagicStorage']) 'Removal left the preview setting behind.'
Require ($removedConfig.mods.MagicStorageProbe.active -and $removedConfig.mods.MagicStorageProbe.custom -ceq 'keep') 'Removal did not restore the original probe setting.'
Require ($removedConfig.mods.'Creative Mode'.menu_key -eq 118) 'Removal changed the manager-owned Creative setting.'
Require (!(Test-Path -LiteralPath (Join-Path $script:Fixture 'mods\magic_storage'))) 'Removal left the Storage preview installed.'
Require (Test-Path -LiteralPath (Join-Path $retainedPath 'magic_storage.dll')) 'Removal did not retain the Storage preview/report directory.'
Add-Result 'remove-preserves-manager-and-unrelated-config' 'passed'

$baseline=Reset-Fixture;$configBefore=Get-Hash (Join-Path $script:Fixture 'shroudtopia.json')
$moveFailure=Invoke-Storage Install @(1)
Require (!$moveFailure.succeeded -and $moveFailure.error -like '*Injected Storage-only Move-Item failure*') 'Injected attach move failure was not reported.'
Require (!(Test-Path -LiteralPath (Join-Path $script:Fixture 'mods\magic_storage'))) 'Failed attach left Storage installed.'
Require ((Get-Hash (Join-Path $script:Fixture 'shroudtopia.json')) -ceq $configBefore) 'Failed attach move changed configuration bytes.'
Assert-ManagerSnapshot $script:Fixture $baseline 'Attach move rollback'
$staging=@(Get-ChildItem -LiteralPath (Join-Path $script:Fixture '.magic-storage-ui-backups') -Directory -Recurse | Where-Object {$_.Name -eq 'magic_storage'})
Require ($staging.Count -eq 1) 'Failed attach did not retain its staged recovery package.'
Require ((Invoke-Storage Install).succeeded) 'Attach retry failed after the first move failure.'
Remove-Storage 'attach move retry'
Add-Result 'attach-first-move-failure-retryable' 'passed'

$baseline=Reset-Fixture;$configBefore=Get-Hash (Join-Path $script:Fixture 'shroudtopia.json')
$lockedInstall=Invoke-StorageWithConfigLock Install
Require (!$lockedInstall.succeeded) 'Attach unexpectedly replaced a locked configuration.'
Require (!(Test-Path -LiteralPath (Join-Path $script:Fixture 'mods\magic_storage'))) 'Failed attach config commit left Storage installed.'
Require ((Get-Hash (Join-Path $script:Fixture 'shroudtopia.json')) -ceq $configBefore) 'Failed attach config commit changed configuration bytes.'
Assert-ManagerSnapshot $script:Fixture $baseline 'Attach config rollback'
Require ((Invoke-Storage Install).succeeded) 'Attach retry failed after a config replacement failure.'
Remove-Storage 'attach config retry'
Add-Result 'attach-config-failure-rollback' 'passed'

$baseline=Reset-Fixture;$null=Attach-Storage 'remove move failure';$configBefore=Get-Hash (Join-Path $script:Fixture 'shroudtopia.json')
$removeMoveFailure=Invoke-Storage Remove @(1)
Require (!$removeMoveFailure.succeeded -and $removeMoveFailure.error -like '*Injected Storage-only Move-Item failure*') 'Injected remove move failure was not reported.'
Require (Test-Path -LiteralPath (Join-Path $script:Fixture 'mods\magic_storage\magic_storage.dll')) 'Failed remove move did not leave Storage installed.'
Require ((Get-Hash (Join-Path $script:Fixture 'shroudtopia.json')) -ceq $configBefore) 'Failed remove move changed configuration bytes.'
Require (!(Test-Path -LiteralPath (Join-Path (Join-Path $script:Fixture '.magic-storage-ui-backups') 'removed'))) 'Failed remove move left an empty marker.'
Assert-ManagerSnapshot $script:Fixture $baseline 'Remove move rollback'
Remove-Storage 'remove move retry'
Add-Result 'remove-first-move-failure-retryable' 'passed'

$baseline=Reset-Fixture;$null=Attach-Storage 'remove config failure';$configBefore=Get-Hash (Join-Path $script:Fixture 'shroudtopia.json')
$lockedRemove=Invoke-StorageWithConfigLock Remove
Require (!$lockedRemove.succeeded) 'Removal unexpectedly replaced a locked configuration.'
Require (Test-Path -LiteralPath (Join-Path $script:Fixture 'mods\magic_storage\magic_storage.dll')) 'Failed remove config commit did not restore Storage.'
Require ((Get-Hash (Join-Path $script:Fixture 'shroudtopia.json')) -ceq $configBefore) 'Failed remove config commit changed configuration bytes.'
Require (!(Test-Path -LiteralPath (Join-Path (Get-ChildItem -LiteralPath (Join-Path $script:Fixture '.magic-storage-ui-backups') -Directory | Select-Object -First 1 -ExpandProperty FullName) 'removed'))) 'Failed remove config commit left an empty marker.'
Assert-ManagerSnapshot $script:Fixture $baseline 'Remove config rollback'
Remove-Storage 'remove config retry'
Add-Result 'remove-config-failure-retryable' 'passed'

$baseline=Reset-Fixture;$configBefore=Get-Hash (Join-Path $script:Fixture 'shroudtopia.json')
Remove-Item -LiteralPath (Join-Path $script:Fixture '.EnshroudedClientMods\active.json') -Force
$missingManager=Invoke-Storage Install
Require (!$missingManager.succeeded) 'Storage attached without a manager receipt.'
Require (!(Test-Path -LiteralPath (Join-Path $script:Fixture 'mods\magic_storage'))) 'Missing manager receipt changed mod files.'
Require ((Get-Hash (Join-Path $script:Fixture 'shroudtopia.json')) -ceq $configBefore) 'Missing manager receipt changed configuration.'
Require ((Get-Hash (Join-Path $script:Fixture 'mods\creative_mode\creative_mode.dll')) -ceq $baseline.creative) 'Missing manager receipt changed Creative.'
Add-Result 'missing-manager-receipt-fails-closed' 'passed'

$baseline=Reset-Fixture;$configBefore=Get-Hash (Join-Path $script:Fixture 'shroudtopia.json')
$managerPath=Join-Path $script:Fixture '.EnshroudedClientMods\active.json'
$manager=Get-Content -LiteralPath $managerPath -Raw | ConvertFrom-Json
$manager.Files[1].After='0'*64;Write-Json $managerPath $manager
$wrongManagerHash=Invoke-Storage Install
Require (!$wrongManagerHash.succeeded) 'Storage attached with a wrong manager-owned host hash.'
Require (!(Test-Path -LiteralPath (Join-Path $script:Fixture 'mods\magic_storage'))) 'Wrong manager hash changed mod files.'
Require ((Get-Hash (Join-Path $script:Fixture 'shroudtopia.json')) -ceq $configBefore) 'Wrong manager hash changed configuration.'
Add-Result 'wrong-manager-owned-hash-fails-closed' 'passed'

$baseline=Reset-Fixture;$configBefore=Get-Hash (Join-Path $script:Fixture 'shroudtopia.json')
[IO.File]::WriteAllText((Join-Path $script:Fixture 'mods\creative_mode\creative_mode.dll'),'unowned replacement',[Text.UTF8Encoding]::new($false))
$wrongCurrentPair=Invoke-Storage Install
Require (!$wrongCurrentPair.succeeded) 'Storage attached with a changed manager-owned Creative DLL.'
Require (!(Test-Path -LiteralPath (Join-Path $script:Fixture 'mods\magic_storage'))) 'Changed Creative hash changed Storage files.'
Require ((Get-Hash (Join-Path $script:Fixture 'shroudtopia.json')) -ceq $configBefore) 'Changed Creative hash changed configuration.'
Add-Result 'wrong-current-pair-fails-closed' 'passed'

$baseline=Reset-Fixture;$configBefore=Get-Hash (Join-Path $script:Fixture 'shroudtopia.json')
$badBuild=Join-Path $script:RunRoot 'wrong-storage-build'
New-Item -ItemType Directory -Path (Join-Path $badBuild 'Release') -Force | Out-Null
$storageBytes=[IO.File]::ReadAllBytes((Join-Path $StorageBuild 'Release\magic_storage_ui_preview.dll'))
Copy-Item -LiteralPath (Join-Path $StorageBuild 'Release\magic_storage_ui_preview.dll') -Destination (Join-Path $badBuild 'Release\magic_storage_ui_preview.dll')
[IO.File]::WriteAllBytes((Join-Path $badBuild 'Release\magic_storage_ui_preview.dll'),($storageBytes+[byte]0x41))
$wrongStorage=Invoke-Storage Install @() $badBuild
Require (!$wrongStorage.succeeded -and $wrongStorage.error -like '*accepted package pin*') 'A wrong Storage build did not fail closed.'
Require (!(Test-Path -LiteralPath (Join-Path $script:Fixture 'mods\magic_storage'))) 'Wrong Storage build changed mod files.'
Require ((Get-Hash (Join-Path $script:Fixture 'shroudtopia.json')) -ceq $configBefore) 'Wrong Storage build changed configuration.'
Add-Result 'wrong-storage-hash-fails-closed' 'passed'

$baseline=Reset-Fixture;$configBefore=Get-Hash (Join-Path $script:Fixture 'shroudtopia.json')
$badSource=Join-Path $script:RunRoot 'wrong-storage-manifest-source';New-Item -ItemType Directory -Path $badSource -Force | Out-Null
foreach($name in @('Install-UiPreview.ps1','native_mod_bindings.cpp','preview.mod.json')) {
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot $name) -Destination (Join-Path $badSource $name)
}
[IO.File]::WriteAllText((Join-Path $badSource 'preview.mod.json'),'unreviewed manifest',[Text.UTF8Encoding]::new($false))
$savedInstaller=$script:Installer
try {$script:Installer=Join-Path $badSource 'Install-UiPreview.ps1';$wrongManifest=Invoke-Storage Install}
finally {$script:Installer=$savedInstaller}
Require (!$wrongManifest.succeeded -and $wrongManifest.error -like '*manifest differs from the accepted package pin*') 'A wrong Storage manifest did not fail closed.'
Require (!(Test-Path -LiteralPath (Join-Path $script:Fixture 'mods\magic_storage'))) 'Wrong Storage manifest changed mod files.'
Require ((Get-Hash (Join-Path $script:Fixture 'shroudtopia.json')) -ceq $configBefore) 'Wrong Storage manifest changed configuration.'
Assert-ManagerSnapshot $script:Fixture $baseline 'Wrong manifest refusal'
Add-Result 'wrong-storage-manifest-fails-closed' 'passed'

$baseline=Reset-Fixture;$configBefore=Get-Hash (Join-Path $script:Fixture 'shroudtopia.json')
$conflict=Join-Path $script:Fixture 'mods\magic_storage';New-Item -ItemType Directory -Path $conflict -Force | Out-Null
[IO.File]::WriteAllText((Join-Path $conflict 'user-owned.txt'),'preserve this conflicting directory',[Text.UTF8Encoding]::new($false))
$conflictHash=Get-Hash (Join-Path $conflict 'user-owned.txt')
$existingConflict=Invoke-Storage Install
Require (!$existingConflict.succeeded) 'Storage attach adopted an unreceipted conflicting directory.'
Require ((Get-Hash (Join-Path $conflict 'user-owned.txt')) -ceq $conflictHash) 'Storage attach changed an unreceipted conflicting file.'
Require ((Get-Hash (Join-Path $script:Fixture 'shroudtopia.json')) -ceq $configBefore) 'Storage attach changed config beside an unreceipted conflicting directory.'
Assert-ManagerSnapshot $script:Fixture $baseline 'Unreceipted conflict refusal'
Add-Result 'unreceipted-storage-conflict-fails-closed' 'passed'

$baseline=Reset-Fixture;$config=Get-Config $script:Fixture;$config.mods | Add-Member MagicStorage ([pscustomobject]@{active=$false;custom='preserve'});Write-Json (Join-Path $script:Fixture 'shroudtopia.json') $config
$configBefore=Get-Hash (Join-Path $script:Fixture 'shroudtopia.json')
$existingSetting=Invoke-Storage Install
Require (!$existingSetting.succeeded) 'Storage attach adopted an existing MagicStorage configuration key.'
Require (!(Test-Path -LiteralPath (Join-Path $script:Fixture 'mods\magic_storage'))) 'Storage attach changed files beside a conflicting config key.'
Require ((Get-Hash (Join-Path $script:Fixture 'shroudtopia.json')) -ceq $configBefore) 'Storage attach changed a conflicting config key.'
Assert-ManagerSnapshot $script:Fixture $baseline 'Existing setting refusal'
Add-Result 'existing-storage-setting-fails-closed' 'passed'

$baseline=Reset-Fixture;$null=Attach-Storage 'future receipt mode';$configBefore=Get-Hash (Join-Path $script:Fixture 'shroudtopia.json')
$receiptPath=Join-Path $script:Fixture 'mods\magic_storage\install-receipt.json'
$validReceiptBytes=[IO.File]::ReadAllBytes($receiptPath)
$future=Get-Content -LiteralPath $receiptPath -Raw | ConvertFrom-Json
$future.mode='StorageOnlyFuture';Write-Json $receiptPath $future
$futureMode=Invoke-Storage Remove
Require (!$futureMode.succeeded -and $futureMode.error -like '*Unsupported preview receipt schema or mode*') 'An unknown receipt mode was not rejected.'
Require (Test-Path -LiteralPath (Join-Path $script:Fixture 'mods\magic_storage\magic_storage.dll')) 'Unknown receipt mode removed Storage.'
Require ((Get-Hash (Join-Path $script:Fixture 'shroudtopia.json')) -ceq $configBefore) 'Unknown receipt mode changed configuration.'
Assert-ManagerSnapshot $script:Fixture $baseline 'Unknown mode refusal'
[IO.File]::WriteAllBytes($receiptPath,$validReceiptBytes)
$wrongPin=Get-Content -LiteralPath $receiptPath -Raw | ConvertFrom-Json
$wrongPin.storageSha256='0'*64;Write-Json $receiptPath $wrongPin
$wrongStorageReceipt=Invoke-Storage Remove
Require (!$wrongStorageReceipt.succeeded -and $wrongStorageReceipt.error -like '*receipt pins*') 'A wrong Storage receipt pin was not rejected.'
Require (Test-Path -LiteralPath (Join-Path $script:Fixture 'mods\magic_storage\magic_storage.dll')) 'Wrong Storage receipt pin removed Storage.'
Require ((Get-Hash (Join-Path $script:Fixture 'shroudtopia.json')) -ceq $configBefore) 'Wrong Storage receipt pin changed configuration.'
[IO.File]::WriteAllBytes($receiptPath,$validReceiptBytes)
Remove-Item -LiteralPath $receiptPath -Force
$missingPreviewReceipt=Invoke-Storage Remove
Require (!$missingPreviewReceipt.succeeded) 'Removal adopted a Storage directory with a missing receipt.'
Require (Test-Path -LiteralPath (Join-Path $script:Fixture 'mods\magic_storage\magic_storage.dll')) 'Missing preview receipt removed Storage.'
Require ((Get-Hash (Join-Path $script:Fixture 'shroudtopia.json')) -ceq $configBefore) 'Missing preview receipt changed configuration.'
Assert-ManagerSnapshot $script:Fixture $baseline 'Missing preview receipt refusal'
Add-Result 'unknown-mode-wrong-pin-and-missing-preview-receipt-fail-closed' 'passed'

$baseline=Reset-Fixture;$null=Attach-Storage 'changed owned setting'
$changedOwned=Get-Config $script:Fixture;$changedOwned.mods.MagicStorage.menu_key=120;Write-Json (Join-Path $script:Fixture 'shroudtopia.json') $changedOwned
$changedConfigHash=Get-Hash (Join-Path $script:Fixture 'shroudtopia.json')
$changedOwnedRemove=Invoke-Storage Remove
Require (!$changedOwnedRemove.succeeded) 'Removal overwrote a user change to a preview-owned config key.'
Require (Test-Path -LiteralPath (Join-Path $script:Fixture 'mods\magic_storage\magic_storage.dll')) 'Refused changed config removal altered Storage files.'
Require ((Get-Hash (Join-Path $script:Fixture 'shroudtopia.json')) -ceq $changedConfigHash) 'Refused changed config removal altered user settings.'
Assert-ManagerSnapshot $script:Fixture $baseline 'Changed owned config refusal'
Add-Result 'changed-owned-config-fails-closed' 'passed'

$report=[pscustomobject]@{
    taskId='APP167'
    result='passed'
    checks=$script:Results.ToArray()
    inputs=[pscustomobject]@{
        storageSha256=(Get-Hash (Join-Path $StorageBuild 'Release\magic_storage_ui_preview.dll'))
        creativeSha256=(Get-Hash $CreativeDll)
        hostSha256=(Get-Hash $HostDll)
        testFixture=$script:Fixture
    }
}
$reportPath=Join-Path $script:RunRoot 'installer_storage_only_test_report.json'
Write-Json $reportPath $report
Write-Output "$($script:Results.Count) Storage-only installer checks passed. Fixture and report retained under: $script:RunRoot"
