[CmdletBinding(SupportsShouldProcess)]
param(
    [ValidateSet('Install','Remove')][string]$Action='Install',
    [Parameter(Mandatory=$true)][string]$GameDirectory,
    [string]$BuildDirectory='E:\Build\creative-ui43-shared-host-20261010\storage-build',
    [string]$CreativeBuildDirectory='E:\Build\creative-ui43-shared-host-20261010\build',
    [ValidateSet('FullPreview','StorageOnly')][string]$Mode='FullPreview'
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$game=(Resolve-Path -LiteralPath $GameDirectory).ProviderPath.TrimEnd('\')
$mods=Join-Path $game 'mods'
$configPath=Join-Path $game 'shroudtopia.json'
$target=Join-Path $mods 'magic_storage'
$receipt=Join-Path $target 'install-receipt.json'
$names=@('magic_storage','native_ui_host','creative_mode','magic_storage_probe')
$storagePreviewPin='403d7b85ab595888da4840e3fb5f07451fb98c0bf8883864a218c01c778ffcab'
$storageManifestPin='16c496b58f799becd074145f174451b8c2cba9e20eeb71190e48d8ce8a325f8f'
$managerCreativePin='befee002983183009129efb76aff80dcb017f5933ea6da6b05aaa7d652ab6250'
$managerHostPin='b6d9e4757d4305bd9aba4325ebd31c3c4e2a31ac6931999f327fe3dbd22f6b46'
$managerCreativePath='mods\creative_mode\creative_mode.dll'
$managerHostPath='native_ui_host\native_ui_host.dll'
function Package-Path([string]$Name) {
    # EML requires a manifest in every direct mods child. The shared DLL is an
    # implementation helper, so keep it outside both mod loaders' scan roots.
    if($Name -eq 'native_ui_host'){return (Join-Path $game $Name)}
    return (Join-Path $mods $Name)
}
function Assert-Closed {
    try {$running=Get-CimInstance Win32_Process -Filter "Name='enshrouded.exe' OR Name='enshrouded_server.exe'" -ErrorAction Stop}
    catch {throw 'Windows CIM could not verify that the game is closed; no installation is allowed.'}
    if($running){throw 'Close Enshrouded and its local server before changing the preview.'}
}
function Assert-Contained([string]$Path) {
    $full=[IO.Path]::GetFullPath($Path)
    if(!$full.StartsWith($game+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Path escapes the game directory.'}
    for($part=$full;$part -and $part.Length -ge $game.Length;$part=Split-Path -Parent $part) {
        if((Test-Path -LiteralPath $part) -and ((Get-Item -LiteralPath $part).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw 'Installer paths must not contain links or reparse points.'
        }
    }
    if(Test-Path -LiteralPath $full -PathType Container) {
        foreach($entry in Get-ChildItem -LiteralPath $full -Recurse -Force) {
            if($entry.Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Installer directory contains a reparse point.'}
        }
    }
}
function Assert-PathComponents([string]$Path) {
    $full=[IO.Path]::GetFullPath($Path)
    if(!$full.StartsWith($game+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Path escapes the game directory.'}
    for($part=$full;$part -and $part.Length -ge $game.Length;$part=Split-Path -Parent $part) {
        if((Test-Path -LiteralPath $part) -and ((Get-Item -LiteralPath $part).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw 'Installer paths must not contain links or reparse points.'
        }
    }
}
function Hash([string]$Path) {(Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()}
function Json-Bytes($Value) {[Text.UTF8Encoding]::new($false).GetBytes(($Value | ConvertTo-Json -Depth 100)+[Environment]::NewLine)}
function File-Map([string]$Directory) {
    $result=[ordered]@{}
    if(Test-Path -LiteralPath $Directory) {
        foreach($file in Get-ChildItem -LiteralPath $Directory -File -Recurse -Force) {
            $result.Add($file.FullName.Substring($Directory.Length+1),(Hash $file.FullName))
        }
    }
    return $result
}
function Check-Map([string]$Directory,$Map) {
    foreach($entry in $Map.PSObject.Properties) {
        $path=Join-Path $Directory $entry.Name;Assert-Contained $path
        if(!(Test-Path -LiteralPath $path -PathType Leaf) -or (Hash $path) -ne $entry.Value){throw 'Package or backup files changed; refusing to overwrite them.'}
    }
}
function Check-UnchangedDirectory([string]$Directory,$Expected) {
    if((Test-Path -LiteralPath $Directory) -ne $Expected.exists){throw 'Mod files changed during staging; nothing was replaced.'}
    $current=File-Map $Directory
    if($current.Count -ne $Expected.files.Count){throw 'Mod files changed during staging; nothing was replaced.'}
    foreach($name in $Expected.files.Keys) {
        if(!$current.Contains($name) -or $current[$name] -ne $Expected.files[$name]) {
            throw 'Mod files changed during staging; nothing was replaced.'
        }
    }
}
function Replace-Config([byte[]]$Bytes,[string]$Expected) {
    if((Hash $configPath) -ne $Expected){throw 'Shroudtopia configuration changed during installation.'}
    $temporary=$configPath+'.'+[guid]::NewGuid().ToString('N')+'.tmp'
    [IO.File]::WriteAllBytes($temporary,$Bytes)
    try {[IO.File]::Replace($temporary,$configPath,[NullString]::Value)}
    finally {if(Test-Path -LiteralPath $temporary){Remove-Item -LiteralPath $temporary}}
}
function Get-ManagerPair([string]$ExpectedReceiptHash) {
    $managerState=Join-Path $game '.EnshroudedClientMods'
    $managerReceipt=Join-Path $managerState 'active.json'
    if(!(Test-Path -LiteralPath $managerState -PathType Container) -or
       !(Test-Path -LiteralPath $managerReceipt -PathType Leaf)) {
        throw 'Storage-only attach requires an installed client-manager receipt.'
    }
    Assert-PathComponents $managerReceipt
    foreach($pendingName in @('removal-transaction.json','receipt-repair-transaction.json')) {
        $pending=Join-Path $managerState $pendingName;Assert-Contained $pending
        if(Test-Path -LiteralPath $pending){throw 'The client manager has pending recovery; resolve it before attaching Storage.'}
    }
    $receiptHash=Hash $managerReceipt
    if($ExpectedReceiptHash -and $receiptHash -ne $ExpectedReceiptHash) {
        throw 'The client manager receipt changed after the Storage preview was attached.'
    }
    try {$manager=Get-Content -LiteralPath $managerReceipt -Raw | ConvertFrom-Json}
    catch {throw 'The client manager receipt is unreadable; no Storage files changed.'}
    if(!$manager -or $manager.Status -cne 'Installed' -or
       $manager.PackageId -notin @('enshrouded-client-mods-more-cheese-v1','enshrouded-client-mods-20261002-v1','enshrouded-client-mods-cloud-v1') -or
       !$manager.Selection -or $manager.Selection.CreativeMode -ne $true) {
        throw 'Storage-only attach requires a healthy installed manager receipt with Creative Mode selected.'
    }
    try {$managerRoot=[IO.Path]::GetFullPath([string]$manager.Root).TrimEnd('\')}
    catch {throw 'The manager receipt has an invalid game root.'}
    if(!$managerRoot -or !$managerRoot.Equals($game,[StringComparison]::OrdinalIgnoreCase)) {
        throw 'The manager receipt belongs to a different game folder.'
    }
    $backupRelative=([string]$manager.Backup).Replace('/','\')
    if($backupRelative -notmatch '\Abackups\\[0-9]{8}-[0-9]{6}-[0-9a-fA-F]{6}\z') {
        throw 'The manager receipt has no recognized rollback snapshot.'
    }
    $managerBackup=Join-Path $managerState $backupRelative;Assert-PathComponents $managerBackup
    if(!(Test-Path -LiteralPath $managerBackup -PathType Container)) {
        throw 'The manager receipt rollback snapshot is missing.'
    }
    if(!$manager.Files -or @($manager.Files).Count -eq 0) {throw 'The manager receipt has no tracked files.'}
    $seen=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    $owned=@{}
    foreach($file in @($manager.Files)) {
        if(!$file -or !$file.PSObject.Properties['Path'] -or !$file.PSObject.Properties['After'] -or !$file.PSObject.Properties['Before']) {
            throw 'The manager receipt contains an incomplete tracked-file record.'
        }
        $relative=([string]$file.Path).Replace('/','\')
        if(!$relative -or !$seen.Add($relative)){throw 'The manager receipt contains an empty or duplicate tracked path.'}
        Assert-Contained (Join-Path $game $relative)
        foreach($hashValue in @($file.Before,$file.After)) {
            if($null -ne $hashValue -and ([string]$hashValue -notmatch '\A[0-9a-fA-F]{64}\z')) {
                throw 'The manager receipt contains an invalid tracked-file hash.'
            }
        }
        if($relative -in @($managerCreativePath,$managerHostPath)) {$owned[$relative]=$file}
    }
    foreach($relative in @($managerCreativePath,$managerHostPath)) {
        if(!$owned.ContainsKey($relative) -or $owned[$relative].After -ine $(if($relative -eq $managerCreativePath){$managerCreativePin}else{$managerHostPin})) {
            throw 'The manager receipt does not own the exact accepted Creative/host pair.'
        }
        if($owned[$relative].Before) {
            $baseline=Join-Path $managerBackup ('originals\'+$relative);Assert-Contained $baseline
            if(!(Test-Path -LiteralPath $baseline -PathType Leaf) -or (Hash $baseline) -ine $owned[$relative].Before) {
                throw 'A manager-owned Creative/host rollback baseline is missing or changed.'
            }
        }
        $installed=Join-Path $game $relative;Assert-Contained $installed
        $expected=if($relative -eq $managerCreativePath){$managerCreativePin}else{$managerHostPin}
        if(!(Test-Path -LiteralPath $installed -PathType Leaf) -or (Hash $installed) -ne $expected) {
            throw 'The installed Creative/host pair does not match the healthy manager receipt.'
        }
    }
    foreach($alternate in @('mods\native_ui_host','mods\native_ui_host.dll')) {
        if(Test-Path -LiteralPath (Join-Path $game $alternate)){throw 'An alternate shared-host path makes manager ownership ambiguous.'}
    }
    return [pscustomobject]@{Path=$managerReceipt;Hash=$receiptHash;Receipt=$manager;Backup=$managerBackup}
}
function Assert-ReviewedModSet {
    $policy=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'native_mod_bindings.cpp') -Raw
    $reviewed=@{}
    foreach($match in [regex]::Matches($policy,'\{"([^"]+\.dll)","([a-f0-9]{64})"\}')) {
        $reviewed[$match.Groups[1].Value.ToLowerInvariant()]=$match.Groups[2].Value
    }
    if($reviewed['creative_mode/creative_mode.dll'] -ne $managerCreativePin -or
       $reviewed['native_ui_host/native_ui_host.dll'] -ne $managerHostPin) {
        throw 'The Storage build policy differs from the manager-owned Creative/host pair.'
    }
    foreach($file in Get-ChildItem -LiteralPath $mods -File -Filter '*.dll' -Recurse) {
        $relative=$file.FullName.Substring($mods.Length+1).Replace('\','/').ToLowerInvariant()
        if($relative -eq 'creative_mode/creative_mode.dll'){continue}
        if(!$reviewed.ContainsKey($relative) -or (Hash $file.FullName) -ne $reviewed[$relative]) {
            throw 'An installed native mod requires a binding review before Storage can be attached.'
        }
    }
}
function Check-StorageDirectory([string]$Directory,$Expected) {
    Check-Map $Directory $Expected
    foreach($entry in Get-ChildItem -LiteralPath $Directory -Directory -Recurse -Force) {
        Assert-Contained $entry.FullName
        throw 'An unexpected directory appeared in the Storage preview; preserve it before removing the preview.'
    }
    foreach($file in Get-ChildItem -LiteralPath $Directory -File -Recurse -Force) {
        $relative=$file.FullName.Substring($Directory.Length+1)
        if(!$Expected.PSObject.Properties[$relative] -and $relative -ne 'install-receipt.json' -and
           $relative -notmatch '^magic-storage-[^\\]+\.tsv$') {
            throw 'A new file appeared in the Storage preview; preserve it before removing the preview.'
        }
    }
}
function Get-SettingsSnapshot($Config) {
    $snapshot=[ordered]@{}
    foreach($key in @('MagicStorage','MagicStorageProbe')) {
        $property=$Config.mods.PSObject.Properties[$key]
        $value=$null
        if($property) {
            $serialized=ConvertTo-Json -InputObject $property.Value -Depth 100 -Compress
            $value=ConvertFrom-Json -InputObject $serialized
        }
        $snapshot[$key]=[pscustomobject]@{exists=($null -ne $property);value=$value}
    }
    return [pscustomobject]$snapshot
}
function Assert-SettingsMatch($Config,$Expected) {
    foreach($key in @('MagicStorage','MagicStorageProbe')) {
        $expectedSetting=$Expected.PSObject.Properties[$key]
        if(!$expectedSetting -or !$expectedSetting.Value.PSObject.Properties['exists'] -or
           !$expectedSetting.Value.PSObject.Properties['value']) {
            throw 'The Storage receipt has incomplete configuration ownership data.'
        }
        $current=$Config.mods.PSObject.Properties[$key]
        if(($null -ne $current) -ne [bool]$expectedSetting.Value.exists) {
            throw 'Magic Storage settings changed; preserve the new settings before changing this preview.'
        }
        if($current) {
            $actual=ConvertTo-Json -InputObject $current.Value -Depth 100 -Compress
            $expectedValue=ConvertTo-Json -InputObject $expectedSetting.Value.value -Depth 100 -Compress
            if($actual -cne $expectedValue) {
                throw 'Magic Storage settings changed; preserve the new settings before changing this preview.'
            }
        }
    }
}
function Same-Map($Left,$Right) {
    if($Left.Count -ne $Right.Count){return $false}
    foreach($name in $Left.Keys) {
        if(!$Right.Contains($name) -or $Left[$name] -cne $Right[$name]){return $false}
    }
    return $true
}
function Hash-Bytes([byte[]]$Bytes) {
    $sha=[Security.Cryptography.SHA256]::Create()
    try {return ([BitConverter]::ToString($sha.ComputeHash($Bytes))).Replace('-','').ToLowerInvariant()}
    finally {$sha.Dispose()}
}
function Get-StorageInstallPlan($Config,[string]$ConfigHash,[byte[]]$ConfigBytes) {
    if(!(Test-Path -LiteralPath $mods -PathType Container)){throw 'The manager-owned mods directory is missing.'}
    if((Test-Path -LiteralPath $target) -or $Config.mods.PSObject.Properties['MagicStorage']) {
        throw 'MagicStorage already exists; remove its owned receipt first.'
    }
    if(Test-Path -LiteralPath (Join-Path $mods 'magic_storage_probe')) {
        throw 'An existing Storage probe directory must be removed through its owner before attaching this preview.'
    }
    if((Hash (Join-Path $game 'enshrouded.exe')) -ne 'af2f5a1227911d8aa06b3908d6bd0211838211cae14ea91099cb57d0df990781') {
        throw 'Unsupported game build.'
    }
    $probe=$Config.mods.PSObject.Properties['MagicStorageProbe']
    if($probe -and ($null -eq $probe.Value -or $probe.Value -isnot [pscustomobject])) {
        throw 'The existing MagicStorageProbe setting is not a recognized object; no configuration changed.'
    }
    $manager=Get-ManagerPair ''
    Assert-ReviewedModSet
    $storageDll=Join-Path $BuildDirectory 'Release\magic_storage_ui_preview.dll'
    if(!(Test-Path -LiteralPath $storageDll -PathType Leaf) -or (Hash $storageDll) -ne $storagePreviewPin) {
        throw 'The Storage preview build differs from the accepted package pin.'
    }
    $storageManifest=Join-Path $PSScriptRoot 'preview.mod.json'
    if(!(Test-Path -LiteralPath $storageManifest -PathType Leaf) -or (Hash $storageManifest) -ne $storageManifestPin) {
        throw 'The Storage preview manifest differs from the accepted package pin.'
    }
    $before=Get-SettingsSnapshot $Config
    if($probe){$Config.mods.MagicStorageProbe | Add-Member active $false -Force}
    $Config.mods | Add-Member MagicStorage ([pscustomobject]@{active=$true;menu_key=0})
    $after=Get-SettingsSnapshot $Config
    $bytes=Json-Bytes $Config
    return [pscustomobject]@{
        ConfigHash=$ConfigHash;ConfigBytes=$ConfigBytes;ConfigAfter=$bytes;ConfigAfterHash=(Hash-Bytes $bytes)
        Manager=$manager;StorageDll=$storageDll;Backup=(Join-Path $game ('.magic-storage-ui-backups\'+[guid]::NewGuid().ToString('N')))
        SettingsBefore=$before;SettingsAfter=$after
    }
}
function Invoke-StorageOnlyInstall($Plan) {
    $backup=$Plan.Backup;Assert-Contained $backup
    $originals=Join-Path $backup 'originals';$stage=Join-Path $backup 'stage'
    $storageStage=Join-Path $stage 'magic_storage'
    $originalConfig=Join-Path $backup 'shroudtopia.before.json'
    $moved=$false;$configCommitted=$false
    $installed=$null;$record=$null
    try {
        New-Item -ItemType Directory -Path $originals,$stage -ErrorAction Stop | Out-Null
        Copy-Item -LiteralPath $configPath -Destination $originalConfig -ErrorAction Stop
        if((Hash $originalConfig) -ne $Plan.ConfigHash){throw 'Configuration changed before Storage staging.'}
        New-Item -ItemType Directory -Path $storageStage -ErrorAction Stop | Out-Null
        Copy-Item -LiteralPath $Plan.StorageDll -Destination (Join-Path $storageStage 'magic_storage.dll') -ErrorAction Stop
        Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'preview.mod.json') -Destination (Join-Path $storageStage 'mod.json') -ErrorAction Stop
        $installed=[pscustomobject]@{magic_storage=[pscustomobject](File-Map $storageStage)}
        if($installed.magic_storage.'magic_storage.dll' -ne $storagePreviewPin -or
           $installed.magic_storage.'mod.json' -ne $storageManifestPin) {
            throw 'Staged Storage preview verification failed.'
        }
        $record=[pscustomobject]@{
            schema=3;mode='StorageOnly';game=$game;backup=$backup;configBefore=$Plan.ConfigHash
            configAfter=$Plan.ConfigAfterHash;managerReceiptHash=$Plan.Manager.Hash
            managerPackageId=$Plan.Manager.Receipt.PackageId
            managerCreativeSha256=$managerCreativePin;managerHostSha256=$managerHostPin
            storageSha256=$storagePreviewPin;storageManifestSha256=$storageManifestPin
            ownership=[pscustomobject]@{
                storageDirectory='mods\magic_storage'
                configKeys=@('MagicStorage','MagicStorageProbe')
                managerReadOnly=@($managerCreativePath,$managerHostPath)
            }
            settingsBefore=$Plan.SettingsBefore;settingsAfter=$Plan.SettingsAfter;installed=$installed
        }
        [IO.File]::WriteAllBytes((Join-Path $storageStage 'install-receipt.json'),(Json-Bytes $record))
        Check-StorageDirectory $storageStage $installed.magic_storage
        Assert-Closed
        if((Hash $configPath) -ne $Plan.ConfigHash){throw 'Configuration changed before Storage commit.'}
        $null=Get-ManagerPair $Plan.Manager.Hash
        if(Test-Path -LiteralPath $target){throw 'Storage target appeared during staging; nothing was overwritten.'}
        Move-Item -LiteralPath $storageStage -Destination $target -ErrorAction Stop
        $moved=$true
        Replace-Config $Plan.ConfigAfter $Plan.ConfigHash
        $configCommitted=$true
        Check-StorageDirectory $target $installed.magic_storage
        if((Hash $configPath) -ne $Plan.ConfigAfterHash){throw 'Storage configuration commit verification failed.'}
        $null=Get-ManagerPair $Plan.Manager.Hash
        Write-Output "Storage-only preview attached. Creative Mode and the shared host remain manager-owned. Backup: $backup"
    } catch {
        $originalFailure=$_.Exception
        $rollbackFailures=[Collections.Generic.List[string]]::new()
        if(Test-Path -LiteralPath $target -PathType Container) {
            try {
                if(!$record -or !(Test-Path -LiteralPath (Join-Path $target 'install-receipt.json') -PathType Leaf)) {
                    throw 'Storage target identity is unverified; preserving it.'
                }
                $currentRecord=Get-Content -LiteralPath (Join-Path $target 'install-receipt.json') -Raw | ConvertFrom-Json
                if($currentRecord.schema -ne 3 -or $currentRecord.mode -cne 'StorageOnly' -or $currentRecord.backup -cne $backup) {
                    throw 'Storage target belongs to another transaction; preserving it.'
                }
                Check-StorageDirectory $target $installed.magic_storage
                if(Test-Path -LiteralPath $storageStage){throw 'Storage staging destination is occupied; preserving both copies.'}
                Move-Item -LiteralPath $target -Destination $storageStage -ErrorAction Stop
                $moved=$false
            } catch {$rollbackFailures.Add("Restore staged Storage package: $($_.Exception.Message)")}
        } elseif($moved) {$rollbackFailures.Add('The attached Storage package disappeared before rollback.')}
        if($configCommitted -or (Hash $configPath) -eq $Plan.ConfigAfterHash) {
            try {Replace-Config $Plan.ConfigBytes $Plan.ConfigAfterHash;$configCommitted=$false}
            catch {$rollbackFailures.Add("Restore original configuration: $($_.Exception.Message)")}
        }
        if($rollbackFailures.Count -eq 0) {
            try {
                if(Test-Path -LiteralPath $target){throw 'Storage target remains after rollback.'}
                if((Hash $configPath) -ne $Plan.ConfigHash){throw 'Original configuration bytes were not restored.'}
                $null=Get-ManagerPair $Plan.Manager.Hash
                if($record) {
                    if(!(Test-Path -LiteralPath $storageStage -PathType Container)){throw 'Staged Storage package was not retained.'}
                    Check-StorageDirectory $storageStage $installed.magic_storage
                }
            } catch {$rollbackFailures.Add("Verify rollback: $($_.Exception.Message)")}
        }
        if($rollbackFailures.Count -gt 0) {
            throw "Storage-only attach failed: $($originalFailure.Message). Rollback also failed: $($rollbackFailures -join '; '). Recovery data retained under: $backup"
        }
        throw "Storage-only attach failed and live state was restored: $($originalFailure.Message). Staging and backup retained under: $backup"
    }
}
function Get-StorageRemovePlan($Record,$Config,[string]$ConfigHash,[byte[]]$ConfigBytes) {
    $allowedFields=@('schema','mode','game','backup','configBefore','configAfter','managerReceiptHash','managerPackageId',
        'managerCreativeSha256','managerHostSha256','storageSha256','storageManifestSha256','ownership','settingsBefore','settingsAfter','installed')
    if($Record.schema -ne 3 -or $Record.mode -cne 'StorageOnly' -or
       @($Record.PSObject.Properties.Name | Where-Object {$_ -notin $allowedFields}).Count -ne 0) {
        throw 'Unsupported Storage-only receipt schema or mode.'
    }
    if(!$Record.game -or !([IO.Path]::GetFullPath([string]$Record.game).TrimEnd('\')).Equals($game,[StringComparison]::OrdinalIgnoreCase)) {
        throw 'Storage receipt belongs to a different game folder.'
    }
    if($Record.storageSha256 -cne $storagePreviewPin -or
       $Record.managerCreativeSha256 -cne $managerCreativePin -or
       $Record.managerHostSha256 -cne $managerHostPin -or
       $Record.storageManifestSha256 -cne $storageManifestPin -or
       [string]$Record.managerReceiptHash -notmatch '\A[0-9a-fA-F]{64}\z') {
        throw 'Storage receipt pins are missing or differ from the accepted package.'
    }
    if(!$Record.ownership -or $Record.ownership.storageDirectory -cne 'mods\magic_storage' -or
       (@($Record.ownership.configKeys) -join '|') -cne 'MagicStorage|MagicStorageProbe' -or
       (@($Record.ownership.managerReadOnly) -join '|') -cne "$managerCreativePath|$managerHostPath" -or
       @($Record.ownership.PSObject.Properties).Count -ne 3) {
        throw 'Storage receipt has unknown or incomplete ownership data.'
    }
    $backup=[IO.Path]::GetFullPath([string]$Record.backup).TrimEnd('\')
    $backupParent=([IO.Path]::GetFullPath((Join-Path $game '.magic-storage-ui-backups'))).TrimEnd('\')
    if(!$backup.StartsWith($backupParent+'\',[StringComparison]::OrdinalIgnoreCase) -or
       (Split-Path -Leaf $backup) -notmatch '\A[0-9a-fA-F]{32}\z') {
        throw 'Storage receipt backup path is invalid.'
    }
    Assert-Contained $backup
    if(!(Test-Path -LiteralPath $backup -PathType Container)){throw 'Storage receipt backup is missing.'}
    $originalConfig=Join-Path $backup 'shroudtopia.before.json';Assert-Contained $originalConfig
    if([string]$Record.configBefore -notmatch '\A[0-9a-fA-F]{64}\z' -or
       !(Test-Path -LiteralPath $originalConfig -PathType Leaf) -or (Hash $originalConfig) -ine $Record.configBefore) {
        throw 'Original configuration backup is missing or changed.'
    }
    if([string]$Record.configAfter -notmatch '\A[0-9a-fA-F]{64}\z' -or
       !$Record.settingsBefore -or !$Record.settingsAfter -or !$Record.installed) {
        throw 'Storage receipt is incomplete.'
    }
    $manager=Get-ManagerPair ([string]$Record.managerReceiptHash)
    if($manager.Receipt.PackageId -cne [string]$Record.managerPackageId) {
        throw 'The manager package identity changed after Storage was attached.'
    }
    $storedMaps=$Record.installed.PSObject.Properties
    if(@($storedMaps).Count -ne 1 -or !$Record.installed.PSObject.Properties['magic_storage']) {
        throw 'Storage receipt does not identify exactly one owned package directory.'
    }
    $storageMap=$Record.installed.magic_storage
    if(@($storageMap.PSObject.Properties).Count -ne 2 -or
       !$storageMap.PSObject.Properties['magic_storage.dll'] -or !$storageMap.PSObject.Properties['mod.json'] -or
       $storageMap.'magic_storage.dll' -cne $storagePreviewPin -or
       $storageMap.'mod.json' -cne $storageManifestPin) {
        throw 'Storage receipt package-file ownership is invalid.'
    }
    if(!$Record.settingsBefore.PSObject.Properties['MagicStorage'] -or
       !$Record.settingsAfter.PSObject.Properties['MagicStorage'] -or
       [bool]$Record.settingsBefore.MagicStorage.exists -or
       ![bool]$Record.settingsAfter.MagicStorage.exists -or
       !$Record.settingsAfter.MagicStorage.value.active -or
       $Record.settingsAfter.MagicStorage.value.menu_key -ne 0) {
        throw 'Storage receipt configuration ownership is invalid.'
    }
    Assert-SettingsMatch $Config $Record.settingsAfter
    Check-StorageDirectory $target $storageMap
    $diskReceipt=Get-Content -LiteralPath $receipt -Raw | ConvertFrom-Json
    if($diskReceipt.schema -ne 3 -or $diskReceipt.mode -cne 'StorageOnly' -or
       $diskReceipt.backup -cne $Record.backup -or $diskReceipt.managerReceiptHash -cne $Record.managerReceiptHash) {
        throw 'Storage receipt identity is inconsistent.'
    }
    $targetMap=File-Map $target
    foreach($key in @('MagicStorage','MagicStorageProbe')) {
        $property=$Config.mods.PSObject.Properties[$key]
        $Config.mods.PSObject.Properties.Remove($key)
        $before=$Record.settingsBefore.PSObject.Properties[$key]
        if(!$before -or !$before.Value.PSObject.Properties['exists'] -or !$before.Value.PSObject.Properties['value']) {
            throw 'Storage receipt has incomplete original configuration ownership data.'
        }
        if($before.Value.exists){$Config.mods | Add-Member $key $before.Value.value}
    }
    $bytes=Json-Bytes $Config
    $beforeConfig=Get-Content -LiteralPath $originalConfig -Raw | ConvertFrom-Json
    if(($Config | ConvertTo-Json -Depth 100 -Compress) -ceq ($beforeConfig | ConvertTo-Json -Depth 100 -Compress)) {
        $bytes=[IO.File]::ReadAllBytes($originalConfig)
    }
    $removed=Join-Path $backup 'removed'
    Assert-Contained $removed
    if(Test-Path -LiteralPath $removed){throw 'This Storage-only preview has already been removed or has retained recovery data.'}
    return [pscustomobject]@{
        Record=$Record;Manager=$manager;Backup=$backup;OriginalConfig=$originalConfig
        ConfigHash=$ConfigHash;ConfigBytes=$ConfigBytes;ConfigAfter=$bytes;ConfigAfterHash=(Hash-Bytes $bytes)
        TargetMap=$targetMap;StorageMap=$storageMap;Removed=$removed
    }
}
function Invoke-StorageOnlyRemove($Plan) {
    $retained=Join-Path $Plan.Removed 'magic_storage';Assert-Contained $Plan.Removed;Assert-Contained $retained
    $markerCreated=$false;$markerCreationTimeUtc=$null;$moved=$false;$configCommitted=$false
    try {
        Assert-Closed
        if((Hash $configPath) -ne $Plan.ConfigHash){throw 'Configuration changed before Storage removal.'}
        $null=Get-ManagerPair $Plan.Manager.Hash
        Check-StorageDirectory $target $Plan.StorageMap
        if(!(Same-Map (File-Map $target) $Plan.TargetMap)){throw 'Storage files changed before removal; preserve them first.'}
        if(Test-Path -LiteralPath $Plan.Removed){throw 'This Storage-only preview has already been removed.'}
        New-Item -ItemType Directory -Path $Plan.Removed -ErrorAction Stop | Out-Null
        $markerCreated=$true;$markerCreationTimeUtc=(Get-Item -LiteralPath $Plan.Removed -Force).CreationTimeUtc
        Move-Item -LiteralPath $target -Destination $retained -ErrorAction Stop
        $moved=$true
        Replace-Config $Plan.ConfigAfter $Plan.ConfigHash
        $configCommitted=$true
        if((Hash $configPath) -ne $Plan.ConfigAfterHash){throw 'Storage removal configuration verification failed.'}
        Check-StorageDirectory $retained $Plan.StorageMap
        $null=Get-ManagerPair $Plan.Manager.Hash
        if(Test-Path -LiteralPath $target){throw 'Storage target remains after removal.'}
        Write-Output "Storage-only preview removed; manager-owned Creative and host remain unchanged. Retained reports: $Plan.Removed"
    } catch {
        $originalFailure=$_.Exception
        $rollbackFailures=[Collections.Generic.List[string]]::new()
        if(Test-Path -LiteralPath $retained -PathType Container) {
            try {
                if(Test-Path -LiteralPath $target){throw 'Storage destination became occupied; retained preview remains under backup.'}
                if(!(Same-Map (File-Map $retained) $Plan.TargetMap)){throw 'Retained Storage files changed; preserving recovery data.'}
                Move-Item -LiteralPath $retained -Destination $target -ErrorAction Stop
                $moved=$false
            } catch {$rollbackFailures.Add("Restore Storage preview: $($_.Exception.Message)")}
        } elseif($moved) {$rollbackFailures.Add('The retained Storage package disappeared before rollback.')}
        if($configCommitted -or (Hash $configPath) -eq $Plan.ConfigAfterHash) {
            try {Replace-Config $Plan.ConfigBytes $Plan.ConfigAfterHash;$configCommitted=$false}
            catch {$rollbackFailures.Add("Restore configuration: $($_.Exception.Message)")}
        }
        if($rollbackFailures.Count -eq 0) {
            try {
                if(!(Test-Path -LiteralPath $target -PathType Container) -or
                   !(Same-Map (File-Map $target) $Plan.TargetMap)){throw 'Storage package was not restored exactly.'}
                if((Hash $configPath) -ne $Plan.ConfigHash){throw 'Configuration bytes were not restored exactly.'}
                $null=Get-ManagerPair $Plan.Manager.Hash
            } catch {$rollbackFailures.Add("Verify rollback: $($_.Exception.Message)")}
        }
        if($rollbackFailures.Count -eq 0 -and $markerCreated -and (Test-Path -LiteralPath $Plan.Removed -PathType Container)) {
            try {
                $marker=Get-Item -LiteralPath $Plan.Removed -Force
                if($marker.Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Removal marker became a reparse point; preserving it.'}
                if($null -eq $markerCreationTimeUtc -or $marker.CreationTimeUtc -ne $markerCreationTimeUtc) {
                    throw 'Removal marker identity changed; preserving it.'
                }
                if(@(Get-ChildItem -LiteralPath $Plan.Removed -Force -Recurse).Count -ne 0) {
                    throw 'Removal marker contains recovery data; preserving it.'
                }
                Remove-Item -LiteralPath $Plan.Removed -Force -ErrorAction Stop
                if(Test-Path -LiteralPath $Plan.Removed){throw 'Empty removal marker could not be removed.'}
            } catch {$rollbackFailures.Add("Clean up new empty removal marker: $($_.Exception.Message)")}
        }
        if($rollbackFailures.Count -gt 0) {
            throw "Storage-only removal failed: $($originalFailure.Message). Rollback also failed: $($rollbackFailures -join '; '). Recovery data retained under: $($Plan.Backup)"
        }
        throw "Storage-only removal failed and live state was restored: $($originalFailure.Message). Recovery files retained under: $($Plan.Backup)"
    }
}
$managerGate=$null
try {
$managerState=Join-Path $game '.EnshroudedClientMods'
if(Test-Path -LiteralPath $managerState) {
    $managerLock=Join-Path $managerState 'operation.lock';Assert-Contained $managerLock
    # Share the manager's existing transaction lock without touching its receipt.
    # Missing/inaccessible locks require coordination, never an assumed idle state.
    try {$managerGate=[IO.File]::Open($managerLock,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::None)}
    catch {throw 'The client manager transaction lock is unavailable; no preview files changed.'}
}
Assert-Closed
foreach($name in $names){Assert-Contained (Package-Path $name)}
Assert-Contained $configPath
$configHash=Hash $configPath
$config=Get-Content -LiteralPath $configPath -Raw | ConvertFrom-Json
if(!$config.mods){throw 'An existing Shroudtopia mods configuration is required.'}
if($Action -eq 'Install') {
    if($Mode -eq 'StorageOnly') {
        $initialBytes=[IO.File]::ReadAllBytes($configPath)
        $plan=Get-StorageInstallPlan $config $configHash $initialBytes
        if(!$PSCmdlet.ShouldProcess($game,'Attach Storage only to the verified manager-owned Creative/host pair')){return}
        Invoke-StorageOnlyInstall $plan
        return
    }
    if((Test-Path -LiteralPath $target) -or $config.mods.PSObject.Properties['MagicStorage']){throw 'MagicStorage already exists; remove this preview with its receipt first.'}
    if((Test-Path -LiteralPath (Package-Path 'native_ui_host')) -or
       (Test-Path -LiteralPath (Join-Path $mods 'native_ui_host'))){throw 'An existing shared host requires a coordinated upgrade; nothing changed.'}
    if((Hash (Join-Path $game 'enshrouded.exe')) -ne 'af2f5a1227911d8aa06b3908d6bd0211838211cae14ea91099cb57d0df990781'){throw 'Unsupported game build.'}
    $preview=Join-Path $BuildDirectory 'Release\magic_storage_ui_preview.dll'
    $hostDll=Join-Path $BuildDirectory 'ui-host\Release\native_ui_host.dll'
    $creativeDll=Join-Path $CreativeBuildDirectory 'Release\creative_client_dev.dll'
    $reviewed=@{}
    $policy=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'native_mod_bindings.cpp') -Raw
    foreach($match in [regex]::Matches($policy,'\{"([^"]+\.dll)","([a-f0-9]{64})"\}')){$reviewed[$match.Groups[1].Value]=$match.Groups[2].Value}
    if((Hash $hostDll) -ne $reviewed['native_ui_host/native_ui_host.dll'] -or
       (Hash $creativeDll) -ne $reviewed['creative_mode/creative_mode.dll']){throw 'Shared host or Creative build differs from the reviewed pair.'}
    foreach($file in Get-ChildItem -LiteralPath $mods -File -Filter '*.dll' -Recurse) {
        $relative=$file.FullName.Substring($mods.Length+1).Replace('\','/').ToLowerInvariant()
        if($relative -in @('creative_mode/creative_mode.dll','magic_storage_probe/magic_storage_probe.dll')){continue}
        if(!$reviewed.ContainsKey($relative) -or (Hash $file.FullName) -ne $reviewed[$relative]){throw 'An installed native mod requires a hotkey review before this preview can be installed.'}
    }
    $backup=Join-Path $game ('.magic-storage-ui-backups\'+[guid]::NewGuid().ToString('N'));Assert-Contained $backup
    $originals=Join-Path $backup 'originals';$stage=Join-Path $backup 'stage'
    $before=@{};$after=@{}
    foreach($key in @('MagicStorage','MagicStorageProbe')) {
        $property=$config.mods.PSObject.Properties[$key]
        $before[$key]=[pscustomobject]@{exists=($null -ne $property);value=$(if($property){$property.Value}else{$null})}
    }
    if($config.mods.PSObject.Properties['MagicStorageProbe']) {
        # Clone before mutation so the saved before-value does not alias it.
        $before['MagicStorageProbe'].value=$before['MagicStorageProbe'].value | ConvertTo-Json -Depth 100 | ConvertFrom-Json
        $config.mods.MagicStorageProbe | Add-Member active $false -Force
    }
    $config.mods | Add-Member MagicStorage ([pscustomobject]@{active=$true;menu_key=0})
    foreach($key in @('MagicStorage','MagicStorageProbe')) {
        $property=$config.mods.PSObject.Properties[$key]
        $after[$key]=[pscustomobject]@{exists=($null -ne $property);value=$(if($property){$property.Value}else{$null})}
    }
    $bytes=Json-Bytes $config
    if(!$PSCmdlet.ShouldProcess($game,'Install native UI preview, paired Creative and shared host with complete backups')){return}
    Assert-Closed
    New-Item -ItemType Directory -Path $originals,$stage | Out-Null
    Copy-Item -LiteralPath $configPath -Destination (Join-Path $backup 'shroudtopia.before.json')
    $old=@{};$installed=@{}
    foreach($name in $names) {
        $directory=Package-Path $name
        $old[$name]=[pscustomobject]@{exists=(Test-Path -LiteralPath $directory);files=(File-Map $directory)}
    }
    foreach($name in @('magic_storage','native_ui_host')){New-Item -ItemType Directory -Path (Join-Path $stage $name) | Out-Null}
    Copy-Item -LiteralPath $preview -Destination (Join-Path $stage 'magic_storage\magic_storage.dll')
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'preview.mod.json') -Destination (Join-Path $stage 'magic_storage\mod.json')
    Copy-Item -LiteralPath $hostDll -Destination (Join-Path $stage 'native_ui_host\native_ui_host.dll')
    if($old['creative_mode'].exists) {
        Copy-Item -LiteralPath (Join-Path $mods 'creative_mode') -Destination (Join-Path $stage 'creative_mode') -Recurse
        Copy-Item -LiteralPath $creativeDll -Destination (Join-Path $stage 'creative_mode\creative_mode.dll') -Force
        $metadataPath=Join-Path $stage 'creative_mode\mod.json'
        $metadata=Get-Content -LiteralPath $metadataPath -Raw | ConvertFrom-Json
        $metadata.version='0.3.0-dev'
        [IO.File]::WriteAllBytes($metadataPath,(Json-Bytes $metadata))
    }
    foreach($name in $names) {$installed[$name]=File-Map (Join-Path $stage $name)}
    if($installed['magic_storage']['magic_storage.dll'] -ne (Hash $preview) -or
       $installed['native_ui_host']['native_ui_host.dll'] -ne (Hash $hostDll)){throw 'Staged package verification failed.'}
    $record=[pscustomobject]@{schema=2;game=$game;backup=$backup;configBefore=$configHash;settingsBefore=$before;settingsAfter=$after;original=$old;installed=$installed}
    [IO.File]::WriteAllBytes((Join-Path $stage 'magic_storage\install-receipt.json'),(Json-Bytes $record))
    $movedOld=[Collections.Generic.List[string]]::new();$movedNew=[Collections.Generic.List[string]]::new()
    try {
        Assert-Closed
        if((Hash $configPath) -ne $configHash){throw 'Configuration changed before commit.'}
        foreach($name in $names){Check-UnchangedDirectory (Package-Path $name) $old[$name]}
        foreach($name in $names) {
            $destination=Package-Path $name;$saved=Join-Path $originals $name;$staged=Join-Path $stage $name
            Assert-Contained $destination;Assert-Contained $saved;Assert-Contained $staged
            if($old[$name].exists){Move-Item -LiteralPath $destination -Destination $saved;$movedOld.Add($name)}
            if(Test-Path -LiteralPath $staged){Move-Item -LiteralPath $staged -Destination $destination;$movedNew.Add($name)}
        }
        Replace-Config $bytes $configHash
    } catch {
        foreach($name in $movedNew){Move-Item -LiteralPath (Package-Path $name) -Destination (Join-Path $stage $name)}
        foreach($name in $movedOld){Move-Item -LiteralPath (Join-Path $originals $name) -Destination (Package-Path $name)}
        throw
    }
    Write-Output "Native UI preview installed; withdrawals remain disabled. Backup: $backup"
} else {
    if(!(Test-Path -LiteralPath $receipt -PathType Leaf)){throw 'No preview receipt exists for this game folder.'}
    $record=Get-Content -LiteralPath $receipt -Raw | ConvertFrom-Json
    if($record.PSObject.Properties['mode']) {
        if($record.schema -ne 3 -or $record.mode -cne 'StorageOnly') {throw 'Unsupported preview receipt schema or mode.'}
        if($PSBoundParameters.ContainsKey('Mode') -and $Mode -ne 'StorageOnly') {throw 'Requested removal mode does not match the Storage-only receipt.'}
        $configBytes=[IO.File]::ReadAllBytes($configPath)
        $plan=Get-StorageRemovePlan $record $config $configHash $configBytes
        if(!$PSCmdlet.ShouldProcess($game,'Remove only the owned Storage preview and its configuration keys')){return}
        Invoke-StorageOnlyRemove $plan
        return
    }
    if($record.schema -ne 2 -or $record.game -ne $game) {throw 'Invalid full-preview receipt schema or mode.'}
    if($PSBoundParameters.ContainsKey('Mode') -and $Mode -ne 'FullPreview') {throw 'Requested removal mode does not match the full-preview receipt.'}
    $backup=[string]$record.backup;Assert-Contained $backup
    $originals=Join-Path $backup 'originals';$removed=Join-Path $backup 'removed'
    Assert-Contained $originals;Assert-Contained $removed
    if(Test-Path -LiteralPath $removed){throw 'This preview has already been removed.'}
    $originalConfig=Join-Path $backup 'shroudtopia.before.json'
    if((Hash $originalConfig) -ne $record.configBefore){throw 'Original configuration backup changed.'}
    foreach($name in $names) {
        Check-Map (Package-Path $name) $record.installed.$name
        if($record.original.$name.exists){Check-Map (Join-Path $originals $name) $record.original.$name.files}
        $directory=Package-Path $name
        if(Test-Path -LiteralPath $directory) {
            foreach($file in Get-ChildItem -LiteralPath $directory -File -Recurse -Force) {
                $relative=$file.FullName.Substring($directory.Length+1)
                if(!$record.installed.$name.PSObject.Properties[$relative] -and
                   !($name -eq 'magic_storage' -and ($relative -eq 'install-receipt.json' -or $relative -match '^magic-storage-[^\\]+\.tsv$'))) {
                    throw 'New files appeared in a replaced mod directory; preserve them before removing the preview.'
                }
            }
        }
    }
    foreach($key in @('MagicStorage','MagicStorageProbe')) {
        $current=$config.mods.PSObject.Properties[$key];$expected=$record.settingsAfter.$key
        if(($null -ne $current) -ne $expected.exists -or ($current -and
           (($current.Value | ConvertTo-Json -Depth 100 -Compress) -ne ($expected.value | ConvertTo-Json -Depth 100 -Compress)))) {
            throw 'Magic Storage settings changed; preserve the new settings before removing the preview.'
        }
        $config.mods.PSObject.Properties.Remove($key)
        if($record.settingsBefore.$key.exists){$config.mods | Add-Member $key $record.settingsBefore.$key.value}
    }
    $bytes=Json-Bytes $config
    # Preserve byte-for-byte configuration when no unrelated settings changed.
    $beforeConfig=Get-Content -LiteralPath $originalConfig -Raw | ConvertFrom-Json
    if(($config | ConvertTo-Json -Depth 100 -Compress) -eq ($beforeConfig | ConvertTo-Json -Depth 100 -Compress)){$bytes=[IO.File]::ReadAllBytes($originalConfig)}
    if(!$PSCmdlet.ShouldProcess($game,'Restore original probe/Creative and configuration; retain preview reports in backup')){return}
    $packageStateBefore=[ordered]@{};$originalStateBefore=[ordered]@{}
    foreach($name in $names) {
        $destination=Package-Path $name;$saved=Join-Path $originals $name
        $packageStateBefore[$name]=[pscustomobject]@{exists=(Test-Path -LiteralPath $destination -PathType Container);files=(File-Map $destination)}
        $originalStateBefore[$name]=[pscustomobject]@{exists=(Test-Path -LiteralPath $saved -PathType Container);files=(File-Map $saved)}
    }
    $movedNew=[Collections.Generic.List[string]]::new();$restoredOld=[Collections.Generic.List[string]]::new()
    $removedMarkerCreated=$false;$removedCreationTimeUtc=$null
    try {
        Assert-Closed
        if(Test-Path -LiteralPath $removed){throw 'This preview has already been removed.'}
        New-Item -ItemType Directory -Path $removed -ErrorAction Stop | Out-Null
        $removedMarkerCreated=$true
        $removedCreationTimeUtc=(Get-Item -LiteralPath $removed -Force).CreationTimeUtc
        foreach($name in $names) {
            $destination=Package-Path $name;$saved=Join-Path $originals $name;$retained=Join-Path $removed $name
            Assert-Contained $destination;Assert-Contained $saved;Assert-Contained $retained
            if(Test-Path -LiteralPath $destination){Move-Item -LiteralPath $destination -Destination $retained;$movedNew.Add($name)}
            if($record.original.$name.exists){Move-Item -LiteralPath $saved -Destination $destination;$restoredOld.Add($name)}
        }
        Replace-Config $bytes $configHash
    } catch {
        $originalFailure=$_.Exception
        $rollbackFailures=[Collections.Generic.List[string]]::new()
        foreach($name in $restoredOld) {
            try {Move-Item -LiteralPath (Package-Path $name) -Destination (Join-Path $originals $name)}
            catch {$rollbackFailures.Add("Restore original ${name}: $($_.Exception.Message)")}
        }
        foreach($name in $movedNew) {
            try {
                $destination=Package-Path $name
                if(Test-Path -LiteralPath $destination){throw "Destination for preview $($name) is occupied; preview copy remains in recovery marker."}
                Move-Item -LiteralPath (Join-Path $removed $name) -Destination $destination
            } catch {$rollbackFailures.Add("Restore preview $($name): $($_.Exception.Message)")}
        }
        if($rollbackFailures.Count -eq 0) {
            try {
                foreach($name in $names) {
                    $destination=Package-Path $name;$saved=Join-Path $originals $name
                    Assert-Contained $destination;Assert-Contained $saved
                    Check-UnchangedDirectory $destination $packageStateBefore[$name]
                    Check-UnchangedDirectory $saved $originalStateBefore[$name]
                }
                if((Hash $configPath) -ne $configHash){throw 'Configuration changed while restoring the failed preview removal.'}
            } catch {$rollbackFailures.Add("Verify restored state: $($_.Exception.Message)")}
        }
        if($rollbackFailures.Count -eq 0 -and $removedMarkerCreated -and (Test-Path -LiteralPath $removed -PathType Container)) {
            try {
                $marker=Get-Item -LiteralPath $removed -Force
                if($marker.Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Removal marker became a reparse point; preserving it.'}
                if($null -eq $removedCreationTimeUtc -or $marker.CreationTimeUtc -ne $removedCreationTimeUtc){throw 'Removal marker identity changed; preserving it.'}
                $markerContents=@(Get-ChildItem -LiteralPath $removed -Force -Recurse)
                if($markerContents.Count -ne 0){throw 'Removal marker contains recovery data; preserving it.'}
                Remove-Item -LiteralPath $removed -Force -ErrorAction Stop
                if(Test-Path -LiteralPath $removed){throw 'Empty removal marker could not be removed.'}
            } catch {$rollbackFailures.Add("Clean up new empty removal marker: $($_.Exception.Message)")}
        }
        if($rollbackFailures.Count -gt 0) {
            throw "Preview removal failed: $($originalFailure.Message). Rollback also failed: $($rollbackFailures -join '; '). Recovery data was retained under: $backup"
        }
        throw
    }
    Write-Output "Original mods restored. Preview reports retained in: $removed"
}
} finally {if($managerGate){$managerGate.Dispose()}}
