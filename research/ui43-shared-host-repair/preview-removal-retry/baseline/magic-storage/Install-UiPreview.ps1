[CmdletBinding(SupportsShouldProcess)]
param(
    [ValidateSet('Install','Remove')][string]$Action='Install',
    [Parameter(Mandatory=$true)][string]$GameDirectory,
    [string]$BuildDirectory='E:\Build\creative-ui43-shared-host-20261010\storage-build',
    [string]$CreativeBuildDirectory='E:\Build\creative-ui43-shared-host-20261010\build'
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$game=(Resolve-Path -LiteralPath $GameDirectory).ProviderPath.TrimEnd('\')
$mods=Join-Path $game 'mods'
$configPath=Join-Path $game 'shroudtopia.json'
$target=Join-Path $mods 'magic_storage'
$receipt=Join-Path $target 'install-receipt.json'
$names=@('magic_storage','native_ui_host','creative_mode','magic_storage_probe')
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
    $record=Get-Content -LiteralPath $receipt -Raw | ConvertFrom-Json
    if($record.schema -ne 2 -or $record.game -ne $game){throw 'Invalid preview receipt.'}
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
    Assert-Closed;New-Item -ItemType Directory -Path $removed | Out-Null
    $movedNew=[Collections.Generic.List[string]]::new();$restoredOld=[Collections.Generic.List[string]]::new()
    try {
        foreach($name in $names) {
            $destination=Package-Path $name;$saved=Join-Path $originals $name;$retained=Join-Path $removed $name
            Assert-Contained $destination;Assert-Contained $saved;Assert-Contained $retained
            if(Test-Path -LiteralPath $destination){Move-Item -LiteralPath $destination -Destination $retained;$movedNew.Add($name)}
            if($record.original.$name.exists){Move-Item -LiteralPath $saved -Destination $destination;$restoredOld.Add($name)}
        }
        Replace-Config $bytes $configHash
    } catch {
        foreach($name in $restoredOld){Move-Item -LiteralPath (Package-Path $name) -Destination (Join-Path $originals $name)}
        foreach($name in $movedNew){Move-Item -LiteralPath (Join-Path $removed $name) -Destination (Package-Path $name)}
        throw
    }
    Write-Output "Original mods restored. Preview reports retained in: $removed"
}
} finally {if($managerGate){$managerGate.Dispose()}}
