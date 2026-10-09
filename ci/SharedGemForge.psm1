Set-StrictMode -Version Latest

$script:GemForgeClientPaths = @(
    'client/game/mods/creative-gem-forges/mod.json',
    'client/game/mods/creative-gem-forges/src/mod.lua',
    'client/game/mods/creative-gem-forges/src/forge_data.lua'
)
$script:GemForgeServerPaths = @(
    'server/game/mods/creative-gem-forges/mod.json',
    'server/game/mods/creative-gem-forges/src/mod.lua',
    'server/game/mods/creative-gem-forges/src/forge_data.lua'
)
$script:GemForgeExpectedPaths = @($script:GemForgeClientPaths) + @($script:GemForgeServerPaths)

function Get-ByteSha256([byte[]]$Bytes) {
    $algorithm = [System.Security.Cryptography.SHA256]::Create()
    try {
        return [System.BitConverter]::ToString($algorithm.ComputeHash($Bytes)).Replace('-', '').ToLowerInvariant()
    } finally {
        $algorithm.Dispose()
    }
}

function Read-ZipEntryBytes([System.IO.Compression.ZipArchiveEntry]$Entry) {
    $input = $Entry.Open()
    $memory = New-Object System.IO.MemoryStream
    try {
        $input.CopyTo($memory)
        return ,$memory.ToArray()
    } finally {
        $input.Dispose()
        $memory.Dispose()
    }
}

function Assert-SafeZipPath([string]$Name, [string]$Label) {
    if ([string]::IsNullOrWhiteSpace($Name) -or $Name.StartsWith('/') -or $Name.Contains('\') -or $Name.Contains(':')) {
        throw "$Label contains an unsafe path: $Name"
    }
    foreach($part in $Name.Split('/')) {
        if ([string]::IsNullOrEmpty($part) -or $part -eq '.' -or $part -eq '..') {
            throw "$Label contains an unsafe path: $Name"
        }
    }
}

function Add-VerifiedSharedGemForgeToStage {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string]$ArchivePath,
        [Parameter(Mandatory)][string]$ExpectedSha256,
        [Parameter(Mandatory)][string]$ExpectedSourceCommit,
        [Parameter(Mandatory)][string]$StageDirectory
    )

    if ($ExpectedSha256 -cnotmatch '^[a-f0-9]{64}$') { throw 'Gem Forge candidate SHA-256 pin is malformed' }
    if ($ExpectedSourceCommit -cnotmatch '^[a-f0-9]{40}$') { throw 'Gem Forge candidate commit pin is malformed' }
    if (!(Test-Path -LiteralPath $ArchivePath -PathType Leaf)) { throw "Gem Forge candidate archive is missing: $ArchivePath" }
    if (!(Test-Path -LiteralPath $StageDirectory -PathType Container)) { throw "Server package stage is missing: $StageDirectory" }
    $actualArchiveHash = (Get-FileHash -LiteralPath $ArchivePath -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actualArchiveHash -cne $ExpectedSha256) { throw 'Gem Forge candidate archive SHA-256 mismatch' }

    Add-Type -AssemblyName System.IO.Compression.FileSystem
    Add-Type -AssemblyName System.IO.Compression
    $zip = [System.IO.Compression.ZipFile]::OpenRead([IO.Path]::GetFullPath($ArchivePath))
    try {
        $archiveNames = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::Ordinal)
        $foldedNames = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
        $entryMap = @{}
        foreach($entry in $zip.Entries) {
            $name = $entry.FullName
            Assert-SafeZipPath $name 'Candidate archive'
            if (!$archiveNames.Add($name) -or !$foldedNames.Add($name)) { throw "Gem Forge candidate archive has a duplicate or case-colliding path: $name" }
            $entryMap[$name] = $entry
        }
        if (!$entryMap.ContainsKey('manifest.json')) { throw 'Gem Forge candidate has no manifest.json' }

        $manifestBytes = Read-ZipEntryBytes $entryMap['manifest.json']
        try {
            $manifest = [Text.Encoding]::UTF8.GetString($manifestBytes) | ConvertFrom-Json -ErrorAction Stop
        } catch {
            throw 'Gem Forge candidate manifest is invalid JSON'
        }
        if ($manifest.schema -ne 1 -or $manifest.schema -is [bool]) { throw 'Gem Forge candidate manifest schema is unsupported' }
        if ($manifest.repository -cne 'Aerox912/enshrouded-creative-mode') { throw 'Gem Forge candidate repository pin mismatch' }
        if ($manifest.sourceCommit -cne $ExpectedSourceCommit) { throw 'Gem Forge candidate source commit mismatch' }
        if ($manifest.sourceCommit -cnotmatch '^[a-f0-9]{40}$' -or [string]::IsNullOrWhiteSpace([string]$manifest.version)) { throw 'Gem Forge candidate provenance is malformed' }
        if ($manifest.packageReady -isnot [bool]) { throw 'Gem Forge candidate packageReady flag is missing or malformed' }

        $records = @{}
        foreach($record in @($manifest.files)) {
            if ($record -isnot [pscustomobject] -or [string]::IsNullOrWhiteSpace([string]$record.path)) { throw 'Gem Forge candidate has a malformed file record' }
            $name = [string]$record.path
            Assert-SafeZipPath $name 'Candidate manifest'
            if ($records.ContainsKey($name)) { throw "Gem Forge candidate manifest has duplicate path: $name" }
            if ($record.size -is [bool] -or $record.size -isnot [ValueType] -or [long]$record.size -lt 0) { throw "Gem Forge candidate has an invalid size: $name" }
            if ([string]$record.sha256 -cnotmatch '^[a-f0-9]{64}$') { throw "Gem Forge candidate has an invalid file hash: $name" }
            $records[$name] = $record
        }

        $archivePayloadNames = @($archiveNames | Where-Object { $_ -cne 'manifest.json' })
        if ($archivePayloadNames.Count -ne $records.Count -or @($archivePayloadNames | Where-Object { !$records.ContainsKey($_) }).Count -gt 0) {
            throw 'Gem Forge candidate archive and manifest inventories differ'
        }
        $modulePaths = @($records.Keys | Where-Object {
            $_.StartsWith('client/game/mods/creative-gem-forges/', [StringComparison]::Ordinal) -or
            $_.StartsWith('server/game/mods/creative-gem-forges/', [StringComparison]::Ordinal)
        })
        $missing = @($script:GemForgeExpectedPaths | Where-Object { $_ -cnotin $modulePaths })
        $extra = @($modulePaths | Where-Object { $_ -cnotin $script:GemForgeExpectedPaths })
        if ($modulePaths.Count -ne 6 -or $missing.Count -gt 0 -or $extra.Count -gt 0) {
            throw "Gem Forge module inventory mismatch; missing=$($missing -join ','); extra=$($extra -join ',')"
        }

        if (!$manifest.PSObject.Properties['sharedModules'] -or !$manifest.sharedModules.PSObject.Properties['creativeGemForges']) {
            throw 'Gem Forge files require sharedModules.creativeGemForges metadata'
        }
        $module = $manifest.sharedModules.creativeGemForges
        if ($module.schema -ne 1 -or $module.schema -is [bool] -or $module.moduleId -cne 'creative-gem-forges') {
            throw 'Gem Forge shared module metadata is invalid'
        }
        if ([string]::IsNullOrWhiteSpace([string]$module.moduleVersion) -or
            [string]$module.sourceItemsJsonSha256 -cnotmatch '^[a-f0-9]{64}$' -or
            [string]$module.forgeMetadataSha256 -cnotmatch '^[a-f0-9]{64}$') {
            throw 'Gem Forge shared module source pins are malformed'
        }
        if (@($module.intendedTargets.clientEditions) -join ',' -cne 'Admin,Regular' -or
            @($module.intendedTargets.serverProfiles) -join ',' -cne 'normal,cheeze') {
            throw 'Gem Forge shared module target editions or profiles changed'
        }
        $nestedRecords = @{}
        foreach($record in @($module.files)) {
            if ($record -isnot [pscustomobject] -or [string]::IsNullOrWhiteSpace([string]$record.path) -or $nestedRecords.ContainsKey([string]$record.path)) {
                throw 'Gem Forge shared module manifest has a malformed or duplicate file record'
            }
            $nestedRecords[[string]$record.path] = $record
        }
        if ($nestedRecords.Count -ne 6 -or @($script:GemForgeExpectedPaths | Where-Object { !$nestedRecords.ContainsKey($_) }).Count -gt 0) {
            throw 'Gem Forge component manifest must list the exact six shared files'
        }

        $moduleBytes = @{}
        foreach($name in $script:GemForgeExpectedPaths) {
            $record = $records[$name]
            $nested = $nestedRecords[$name]
            if ($record.audience -cne 'shared' -or $record.adminOnly -isnot [bool] -or $record.adminOnly -ne $false) {
                throw "Gem Forge file is not declared shared for both editions: $name"
            }
            if ($nested.path -cne $record.path -or [long]$nested.size -ne [long]$record.size -or
                $nested.sha256 -cne $record.sha256 -or $nested.audience -cne $record.audience -or
                $nested.adminOnly -isnot [bool] -or $nested.adminOnly -ne $false) {
                throw "Gem Forge component manifest differs from candidate file record: $name"
            }
            $bytes = Read-ZipEntryBytes $entryMap[$name]
            if ($bytes.Length -ne [long]$record.size -or (Get-ByteSha256 $bytes) -cne $record.sha256) {
                throw "Gem Forge candidate member hash or size mismatch: $name"
            }
            $moduleBytes[$name] = $bytes
        }
        foreach($clientPath in $script:GemForgeClientPaths) {
            $serverPath = $clientPath.Replace('client/game/', 'server/game/')
            if ((Get-ByteSha256 $moduleBytes[$clientPath]) -cne (Get-ByteSha256 $moduleBytes[$serverPath])) {
                throw "Gem Forge client and server bytes differ: $clientPath"
            }
        }

        $moduleMetadata = [Text.Encoding]::UTF8.GetString($moduleBytes['client/game/mods/creative-gem-forges/mod.json']) | ConvertFrom-Json -ErrorAction Stop
        if ($moduleMetadata.id -cne 'creative-gem-forges' -or @($moduleMetadata.capabilities) -cnotcontains 'patch') {
            throw 'Gem Forge EML module metadata or patch capability is missing'
        }
        $runtimeText = [Text.Encoding]::UTF8.GetString($moduleBytes['client/game/mods/creative-gem-forges/src/mod.lua'])
        $dataText = [Text.Encoding]::UTF8.GetString($moduleBytes['client/game/mods/creative-gem-forges/src/forge_data.lua'])
        if ($runtimeText -notmatch 'require\s*\(\s*["'']forge_data["'']\s*\)' -or
            $dataText -notmatch [regex]::Escape([string]$module.sourceItemsJsonSha256)) {
            throw 'Gem Forge runtime files do not match the pinned source metadata'
        }

        $moduleDirectory = Join-Path $StageDirectory 'mods/creative-gem-forges'
        $ancestor = [IO.Path]::GetFullPath($StageDirectory)
        foreach($part in @('mods', 'creative-gem-forges')) {
            $ancestor = Join-Path $ancestor $part
            if (Test-Path -LiteralPath $ancestor) {
                $ancestorInfo = Get-Item -LiteralPath $ancestor -Force
                if ($ancestorInfo.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Server package stage contains a linked Gem Forge path' }
            }
        }
        if (Test-Path -LiteralPath $moduleDirectory) {
            foreach($existing in Get-ChildItem -LiteralPath $moduleDirectory -Recurse -Force) {
                if ($existing.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Server package stage contains a linked Gem Forge path' }
                if ($existing -is [IO.FileInfo]) {
                    $relative = $existing.FullName.Substring($moduleDirectory.Length).TrimStart('\').Replace('\', '/')
                    if ($relative -cnotin @('mod.json', 'src/mod.lua', 'src/forge_data.lua')) {
                        throw "Server package stage contains an unexpected Gem Forge file: $relative"
                    }
                }
            }
        }
        foreach($serverPath in $script:GemForgeServerPaths) {
            $relative = $serverPath.Substring('server/game/'.Length)
            $destination = Join-Path $StageDirectory ($relative.Replace('/', '\'))
            $parent = Split-Path -Parent $destination
            New-Item -ItemType Directory -Path $parent -Force | Out-Null
            if (Test-Path -LiteralPath $destination) {
                $destinationInfo = Get-Item -LiteralPath $destination -Force
                if ($destinationInfo.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Server package stage has a linked file: $destination" }
            }
            [IO.File]::WriteAllBytes($destination, $moduleBytes[$serverPath])
        }

        $manifestHash = Get-ByteSha256 $manifestBytes
        $componentFiles = @($script:GemForgeServerPaths | ForEach-Object {
            $record = $records[$_]
            [ordered]@{ path=$_.Substring('server/game/'.Length); size=[long]$record.size; sha256=[string]$record.sha256 }
        })
        return [ordered]@{
            candidateArchiveSha256=$actualArchiveHash
            candidateSourceCommit=[string]$manifest.sourceCommit
            candidateManifestSha256=$manifestHash
            moduleId=[string]$module.moduleId
            moduleVersion=[string]$module.moduleVersion
            files=$componentFiles
        }
    } finally {
        $zip.Dispose()
    }
}

function Add-VerifiedSharedGemForgeToPreparedOutput {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string]$PackageRoot,
        [Parameter(Mandatory)][string]$OutputDirectory,
        [Parameter(Mandatory)]$GemForgeMetadata
    )

    $expectedPaths = @(
        'mods/creative-gem-forges/mod.json',
        'mods/creative-gem-forges/src/mod.lua',
        'mods/creative-gem-forges/src/forge_data.lua'
    )
    foreach($field in @('candidateArchiveSha256', 'candidateManifestSha256')) {
        if ([string]$GemForgeMetadata.$field -cnotmatch '^[a-f0-9]{64}$') { throw "Gem Forge prepared-output provenance is malformed: $field" }
    }
    if ([string]$GemForgeMetadata.candidateSourceCommit -cnotmatch '^[a-f0-9]{40}$' -or
        $GemForgeMetadata.moduleId -cne 'creative-gem-forges' -or
        [string]::IsNullOrWhiteSpace([string]$GemForgeMetadata.moduleVersion)) {
        throw 'Gem Forge prepared-output provenance is malformed'
    }

    $records = @{}
    foreach($record in @($GemForgeMetadata.files)) {
        if ($record -isnot [pscustomobject] -and $record -isnot [System.Collections.IDictionary]) { throw 'Gem Forge prepared-output file record is malformed' }
        $path = [string]$record.path
        Assert-SafeZipPath $path 'Gem Forge prepared-output manifest'
        if ($path -cnotin $expectedPaths -or $records.ContainsKey($path)) { throw "Gem Forge prepared-output inventory is invalid: $path" }
        if ($record.size -is [bool] -or $record.size -isnot [ValueType] -or [long]$record.size -lt 0 -or [long]$record.size -ne $record.size) {
            throw "Gem Forge prepared-output size is invalid: $path"
        }
        if ([string]$record.sha256 -cnotmatch '^[a-f0-9]{64}$') { throw "Gem Forge prepared-output hash is invalid: $path" }
        $records[$path] = $record
    }
    if ($records.Count -ne $expectedPaths.Count -or @($expectedPaths | Where-Object { !$records.ContainsKey($_) }).Count -gt 0) {
        throw 'Gem Forge prepared-output manifest must contain the exact three server files'
    }

    $packageRootFull = [IO.Path]::GetFullPath($PackageRoot)
    $outputRootFull = [IO.Path]::GetFullPath($OutputDirectory)
    if (!(Test-Path -LiteralPath $packageRootFull -PathType Container) -or !(Test-Path -LiteralPath $outputRootFull -PathType Container)) {
        throw 'Gem Forge package root or prepared output directory is missing'
    }
    foreach($root in @($packageRootFull, $outputRootFull)) {
        if ((Get-Item -LiteralPath $root -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Gem Forge package or prepared output root is linked' }
    }
    $packagePrefix = $packageRootFull.TrimEnd('\','/') + [IO.Path]::DirectorySeparatorChar
    $outputPrefix = $outputRootFull.TrimEnd('\','/') + [IO.Path]::DirectorySeparatorChar
    if ($outputRootFull.Equals($packageRootFull, [StringComparison]::OrdinalIgnoreCase) -or
        $outputRootFull.StartsWith($packagePrefix, [StringComparison]::OrdinalIgnoreCase) -or
        $packageRootFull.StartsWith($outputPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Gem Forge package and prepared output directories must be separate'
    }

    foreach($relativeDirectory in @('mods', 'mods/creative-gem-forges', 'mods/creative-gem-forges/src')) {
        $sourceDirectory = Join-Path $packageRootFull ($relativeDirectory.Replace('/', '\'))
        if (Test-Path -LiteralPath $sourceDirectory) {
            if ((Get-Item -LiteralPath $sourceDirectory -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Gem Forge package contains a linked module path' }
        }
    }
    $packageModuleDirectory = Join-Path $packageRootFull 'mods/creative-gem-forges'
    if (!(Test-Path -LiteralPath $packageModuleDirectory -PathType Container)) { throw 'Gem Forge server module is missing from the package' }
    $packageModulePrefix = [IO.Path]::GetFullPath($packageModuleDirectory).TrimEnd('\','/') + [IO.Path]::DirectorySeparatorChar
    $packageFiles = @(Get-ChildItem -LiteralPath $packageModuleDirectory -Recurse -File -Force)
    $packageRelativePaths = @($packageFiles | ForEach-Object { $_.FullName.Substring($packageModulePrefix.Length).Replace('\', '/') })
    $expectedModuleRelativePaths = @('mod.json', 'src/mod.lua', 'src/forge_data.lua')
    if ($packageRelativePaths.Count -ne $expectedModuleRelativePaths.Count -or @($expectedModuleRelativePaths | Where-Object { $_ -cnotin $packageRelativePaths }).Count -gt 0) {
        throw 'Gem Forge package module directory does not contain the exact three server files'
    }
    foreach($file in $packageFiles) {
        if ($file.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Gem Forge package contains a linked module file' }
    }

    $moduleBytes = @{}
    foreach($path in $expectedPaths) {
        $sourcePath = Join-Path $packageRootFull ($path.Replace('/', '\'))
        $bytes = [IO.File]::ReadAllBytes($sourcePath)
        $record = $records[$path]
        if ($bytes.Length -ne [long]$record.size -or (Get-ByteSha256 $bytes) -cne [string]$record.sha256) {
            throw "Gem Forge package file differs from its verified manifest: $path"
        }
        $moduleBytes[$path] = $bytes
    }

    $preparedManifestPath = Join-Path $outputRootFull 'prepared-files.json'
    if (!(Test-Path -LiteralPath $preparedManifestPath -PathType Leaf) -or
        (Get-Item -LiteralPath $preparedManifestPath -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) {
        throw 'Prepared output has no regular prepared-files.json manifest'
    }
    try {
        $preparedManifest = Get-Content -LiteralPath $preparedManifestPath -Raw | ConvertFrom-Json -ErrorAction Stop
    } catch {
        throw 'Prepared output prepared-files.json is invalid JSON'
    }
    if ($preparedManifest -isnot [pscustomobject]) { throw 'Prepared output prepared-files.json must be an object' }
    foreach($path in $expectedPaths) {
        if ($preparedManifest.PSObject.Properties[$path]) { throw "Prepared output already tracks the Gem Forge file: $path" }
    }

    $outputModuleDirectory = Join-Path $outputRootFull 'mods/creative-gem-forges'
    foreach($relativeDirectory in @('mods', 'mods/creative-gem-forges', 'mods/creative-gem-forges/src')) {
        $destinationDirectory = Join-Path $outputRootFull ($relativeDirectory.Replace('/', '\'))
        if (Test-Path -LiteralPath $destinationDirectory) {
            if ((Get-Item -LiteralPath $destinationDirectory -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Prepared output contains a linked Gem Forge path' }
        }
    }
    if (Test-Path -LiteralPath $outputModuleDirectory) {
        $existingModuleFiles = @(Get-ChildItem -LiteralPath $outputModuleDirectory -Recurse -File -Force)
        if ($existingModuleFiles.Count -gt 0) { throw 'Prepared output already contains Gem Forge files; refusing to overwrite' }
    }

    foreach($path in $expectedPaths) {
        $preparedManifest | Add-Member -MemberType NoteProperty -Name $path -Value ([pscustomobject]@{sha256=[string]$records[$path].sha256})
    }
    $manifestText = (ConvertTo-Json -InputObject $preparedManifest -Depth 100) + [Environment]::NewLine
    $manifestTemporaryPath = Join-Path $outputRootFull ('.prepared-files-' + [guid]::NewGuid().ToString('N') + '.tmp')
    $originalPreparedManifestBytes = [IO.File]::ReadAllBytes($preparedManifestPath)
    $manifestWriteStarted = $false
    $createdFiles = @()
    $createdDirectories = @()
    try {
        foreach($path in $expectedPaths) {
            $destination = Join-Path $outputRootFull ($path.Replace('/', '\'))
            $parent = Split-Path -Parent $destination
            $missingDirectories = @()
            $cursor = $parent
            while (!(Test-Path -LiteralPath $cursor -PathType Container)) {
                $missingDirectories += $cursor
                $cursor = Split-Path -Parent $cursor
            }
            [array]::Reverse($missingDirectories)
            foreach($directory in $missingDirectories) {
                New-Item -ItemType Directory -Path $directory | Out-Null
                $createdDirectories += $directory
            }
            [IO.File]::WriteAllBytes($destination, $moduleBytes[$path])
            $createdFiles += $destination
        }
        [IO.File]::WriteAllText($manifestTemporaryPath, $manifestText, [Text.UTF8Encoding]::new($false))
        $manifestBytes = [IO.File]::ReadAllBytes($manifestTemporaryPath)
        $manifestWriteStarted = $true
        [IO.File]::WriteAllBytes($preparedManifestPath, $manifestBytes)
        [IO.File]::Delete($manifestTemporaryPath)
    } catch {
        if ($manifestWriteStarted) {
            try { [IO.File]::WriteAllBytes($preparedManifestPath, $originalPreparedManifestBytes) }
            catch { throw "Gem Forge prepared-files update failed and rollback could not restore the original manifest: $($_.Exception.Message)" }
        }
        foreach($createdFile in $createdFiles) {
            if (Test-Path -LiteralPath $createdFile -PathType Leaf) { [IO.File]::Delete($createdFile) }
        }
        [array]::Reverse($createdDirectories)
        foreach($createdDirectory in $createdDirectories) {
            if ((Test-Path -LiteralPath $createdDirectory -PathType Container) -and
                !(Get-ChildItem -LiteralPath $createdDirectory -Force | Select-Object -First 1)) {
                [IO.Directory]::Delete($createdDirectory)
            }
        }
        if (Test-Path -LiteralPath $manifestTemporaryPath -PathType Leaf) { [IO.File]::Delete($manifestTemporaryPath) }
        throw
    }

    return [ordered]@{
        moduleId=[string]$GemForgeMetadata.moduleId
        moduleVersion=[string]$GemForgeMetadata.moduleVersion
        fileCount=$expectedPaths.Count
        files=@($expectedPaths | ForEach-Object { [ordered]@{path=$_;sha256=[string]$records[$_].sha256;size=[long]$records[$_].size} })
    }
}

Export-ModuleMember -Function Add-VerifiedSharedGemForgeToStage, Add-VerifiedSharedGemForgeToPreparedOutput
