$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem
Add-Type -AssemblyName System.IO.Compression
Import-Module (Join-Path $PSScriptRoot '../ci/SharedGemForge.psm1') -Force

function Get-TestSha([byte[]]$Bytes) {
 $algorithm=[System.Security.Cryptography.SHA256]::Create()
 try { [System.BitConverter]::ToString($algorithm.ComputeHash($Bytes)).Replace('-','').ToLowerInvariant() }
 finally { $algorithm.Dispose() }
}

function Write-TestArchive([string]$Path,[hashtable]$Files,$Manifest) {
 $stream=[IO.File]::Open($Path,[IO.FileMode]::CreateNew,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
 $archive=[System.IO.Compression.ZipArchive]::new($stream,[IO.Compression.ZipArchiveMode]::Create,$false)
 try {
  foreach($name in @($Files.Keys | Sort-Object)) {
   $entry=$archive.CreateEntry($name)
   $entryStream=$entry.Open()
   try { $entryStream.Write($Files[$name],0,$Files[$name].Length) } finally { $entryStream.Dispose() }
  }
  $manifestEntry=$archive.CreateEntry('manifest.json')
  $manifestStream=$manifestEntry.Open()
  $bytes=[Text.UTF8Encoding]::new($false).GetBytes(($Manifest | ConvertTo-Json -Depth 20))
  try { $manifestStream.Write($bytes,0,$bytes.Length) } finally { $manifestStream.Dispose() }
 } finally { $archive.Dispose(); $stream.Dispose() }
}

function Assert-Rejected([scriptblock]$Action,[string]$Expected,[string]$Label) {
 $rejected=$false
 try { & $Action } catch {
  $rejected=$true
  if ($_.Exception.Message -notlike "*$Expected*") { throw "$Label rejected for the wrong reason: $($_.Exception.Message)" }
 }
 if (!$rejected) { throw "$Label was unexpectedly accepted" }
}

function Copy-TestMap($Value) {
 $copy=[ordered]@{}
 foreach($entry in $Value.GetEnumerator()) { $copy[$entry.Key]=$entry.Value }
 return $copy
}

$temporaryRoot=[IO.Path]::GetFullPath($env:TEMP)
$testRoot=Join-Path $temporaryRoot ('GemForgePackageTest-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $testRoot | Out-Null
try {
 $sourceCommit='a'*40
 $sourceHash='b'*64
 $moduleData=@{
  'mod.json'=[Text.UTF8Encoding]::new($false).GetBytes('{"id":"creative-gem-forges","capabilities":["patch"]}')
  'src/mod.lua'=[Text.UTF8Encoding]::new($false).GetBytes('local DATA = require("forge_data")')
  'src/forge_data.lua'=[Text.UTF8Encoding]::new($false).GetBytes("sourceItemsJsonSha256 = `"$sourceHash`"`nentries = {}`n")
 }
 $files=@{}
 foreach($root in @('client/game/mods/creative-gem-forges/','server/game/mods/creative-gem-forges/')) {
  foreach($name in $moduleData.Keys) { $files[$root+$name]=$moduleData[$name] }
 }
 $records=@(
  foreach($name in @($files.Keys | Sort-Object)) {
   [ordered]@{path=$name;size=$files[$name].Length;sha256=(Get-TestSha $files[$name]);audience='shared';adminOnly=$false}
  }
 )
 $module=[ordered]@{
  schema=1
  moduleId='creative-gem-forges'
  moduleVersion='1.0.0'
  sourceItemsJsonSha256=$sourceHash
  forgeMetadataSha256='c'*64
  intendedTargets=[ordered]@{clientEditions=@('Admin','Regular');serverProfiles=@('normal','cheeze')}
  files=$records
 }
 $manifest=[ordered]@{
  schema=1
  version='0.3.0-rc.5'
  packageReady=$false
  sourceCommit=$sourceCommit
  repository='Aerox912/enshrouded-creative-mode'
  sharedModules=@{creativeGemForges=$module}
  files=$records
 }
 $archive=Join-Path $testRoot 'candidate.zip'
 Write-TestArchive $archive $files $manifest
 $archiveHash=(Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()

 foreach($profile in @('normal','cheeze')) {
  $stage=Join-Path $testRoot $profile
  New-Item -ItemType Directory -Path $stage | Out-Null
  $result=Add-VerifiedSharedGemForgeToStage -ArchivePath $archive -ExpectedSha256 $archiveHash -ExpectedSourceCommit $sourceCommit -StageDirectory $stage
  if ($result.files.Count -ne 3) { throw "$profile package provenance did not describe three module files" }
  foreach($relative in @('mod.json','src/mod.lua','src/forge_data.lua')) {
   $target=Join-Path $stage ('mods/creative-gem-forges/'+$relative.Replace('/','\'))
   if (!(Test-Path -LiteralPath $target -PathType Leaf)) { throw "$profile package omitted $relative" }
   $actual=[IO.File]::ReadAllBytes($target)
   if ((Get-TestSha $actual) -cne (Get-TestSha $moduleData[$relative])) { throw "$profile package changed $relative" }
   $record=$result.files | Where-Object path -CEQ ('mods/creative-gem-forges/'+$relative)
   if (!$record -or $record.sha256 -cne (Get-TestSha $actual) -or [long]$record.size -ne $actual.Length) { throw "$profile package provenance differs for $relative" }
  }

  $preparedOutput=Join-Path $testRoot ($profile+'-prepared')
  New-Item -ItemType Directory -Path $preparedOutput | Out-Null
  $originalImportBytes=[Text.UTF8Encoding]::new($false).GetBytes('prepared original import')
  [IO.File]::WriteAllBytes((Join-Path $preparedOutput 'enshrouded_server.kfc'),$originalImportBytes)
  $workshopBytes=[Text.UTF8Encoding]::new($false).GetBytes('Workshop 20x remains unchanged')
  $otherModBytes=[Text.UTF8Encoding]::new($false).GetBytes('keep other selected server mod')
  $workshopPath=Join-Path $preparedOutput 'mods/XHL-Workshop-Speed-20x/config.txt'
  $otherModPath=Join-Path $preparedOutput 'mods/other-selected-mod/mod.lua'
  New-Item -ItemType Directory -Path (Split-Path -Parent $workshopPath) -Force | Out-Null
  New-Item -ItemType Directory -Path (Split-Path -Parent $otherModPath) -Force | Out-Null
  [IO.File]::WriteAllBytes($workshopPath,$workshopBytes)
  [IO.File]::WriteAllBytes($otherModPath,$otherModBytes)
  $preparedManifest=[ordered]@{
   'enshrouded_server.kfc'=[ordered]@{sha256=(Get-TestSha $originalImportBytes)}
   'mods/XHL-Workshop-Speed-20x/config.txt'=[ordered]@{sha256=(Get-TestSha $workshopBytes)}
  }
  [IO.File]::WriteAllText((Join-Path $preparedOutput 'prepared-files.json'),($preparedManifest | ConvertTo-Json -Depth 10),[Text.UTF8Encoding]::new($false))
  $preparedResult=Add-VerifiedSharedGemForgeToPreparedOutput -PackageRoot $stage -OutputDirectory $preparedOutput -GemForgeMetadata $result
  if($preparedResult.fileCount -ne 3) { throw "$profile prepared output did not report three verified module files" }
  foreach($relative in @('mod.json','src/mod.lua','src/forge_data.lua')) {
   $preparedPath=Join-Path $preparedOutput ('mods/creative-gem-forges/'+$relative.Replace('/','\'))
   if(!(Test-Path -LiteralPath $preparedPath -PathType Leaf)) { throw "$profile prepared output omitted $relative" }
   $actual=[IO.File]::ReadAllBytes($preparedPath)
   if((Get-TestSha $actual) -cne (Get-TestSha $moduleData[$relative])) { throw "$profile prepared output changed $relative" }
   $manifestAfter=Get-Content -LiteralPath (Join-Path $preparedOutput 'prepared-files.json') -Raw | ConvertFrom-Json
   $manifestRecord=$manifestAfter.PSObject.Properties['mods/creative-gem-forges/'+$relative]
   if(!$manifestRecord -or $manifestRecord.Value.sha256 -cne (Get-TestSha $actual)) { throw "$profile prepared-files.json omitted or mis-hashed $relative" }
  }
  if((Get-TestSha ([IO.File]::ReadAllBytes($workshopPath))) -cne (Get-TestSha $workshopBytes) -or
     (Get-TestSha ([IO.File]::ReadAllBytes($otherModPath))) -cne (Get-TestSha $otherModBytes) -or
     (Get-TestSha ([IO.File]::ReadAllBytes((Join-Path $preparedOutput 'enshrouded_server.kfc')))) -cne (Get-TestSha $originalImportBytes)) {
   throw "$profile prepared output changed an unrelated mod or original import"
  }
  $manifestAfter=Get-Content -LiteralPath (Join-Path $preparedOutput 'prepared-files.json') -Raw | ConvertFrom-Json
  if($manifestAfter.'enshrouded_server.kfc'.sha256 -cne (Get-TestSha $originalImportBytes) -or
     $manifestAfter.'mods/XHL-Workshop-Speed-20x/config.txt'.sha256 -cne (Get-TestSha $workshopBytes) -or
     @($manifestAfter.PSObject.Properties).Count -ne 5) { throw "$profile prepared-files.json did not preserve existing records" }
  if(@(Get-ChildItem -LiteralPath $preparedOutput -Filter '.prepared-files-*.tmp' -Force).Count -ne 0) { throw "$profile prepared output retained a temporary manifest file" }
 }

 Assert-Rejected { Add-VerifiedSharedGemForgeToStage -ArchivePath $archive -ExpectedSha256 ('0'*64) -ExpectedSourceCommit $sourceCommit -StageDirectory (Join-Path $testRoot 'normal') } 'archive SHA-256 mismatch' 'wrong archive hash'
 Assert-Rejected { Add-VerifiedSharedGemForgeToStage -ArchivePath $archive -ExpectedSha256 $archiveHash -ExpectedSourceCommit ('d'*40) -StageDirectory (Join-Path $testRoot 'normal') } 'source commit mismatch' 'wrong source commit'

 $missingFiles=@{}; foreach($name in $files.Keys) { if($name -cne 'server/game/mods/creative-gem-forges/src/mod.lua') { $missingFiles[$name]=$files[$name] } }
 $missingRecords=@($records | Where-Object { $_.path -cne 'server/game/mods/creative-gem-forges/src/mod.lua' })
 $missingManifest=Copy-TestMap $manifest; $missingManifest.files=$missingRecords; $missingModule=Copy-TestMap $module; $missingModule.files=$missingRecords; $missingManifest.sharedModules=@{creativeGemForges=$missingModule}
 $missingArchive=Join-Path $testRoot 'missing.zip'; Write-TestArchive $missingArchive $missingFiles $missingManifest
 Assert-Rejected { Add-VerifiedSharedGemForgeToStage -ArchivePath $missingArchive -ExpectedSha256 (Get-FileHash -LiteralPath $missingArchive -Algorithm SHA256).Hash.ToLowerInvariant() -ExpectedSourceCommit $sourceCommit -StageDirectory (Join-Path $testRoot 'normal') } 'inventory mismatch' 'missing module file'

 $wrongKindRecords=@(foreach($record in $records) { if($record.path -ceq 'client/game/mods/creative-gem-forges/mod.json') { $copy=Copy-TestMap $record; $copy.adminOnly=$true; $copy } else { $record } })
 $wrongKindManifest=Copy-TestMap $manifest; $wrongKindManifest.files=$wrongKindRecords; $wrongKindModule=Copy-TestMap $module; $wrongKindModule.files=$wrongKindRecords; $wrongKindManifest.sharedModules=@{creativeGemForges=$wrongKindModule}
 $wrongKindArchive=Join-Path $testRoot 'wrong-kind.zip'; Write-TestArchive $wrongKindArchive $files $wrongKindManifest
 Assert-Rejected { Add-VerifiedSharedGemForgeToStage -ArchivePath $wrongKindArchive -ExpectedSha256 (Get-FileHash -LiteralPath $wrongKindArchive -Algorithm SHA256).Hash.ToLowerInvariant() -ExpectedSourceCommit $sourceCommit -StageDirectory (Join-Path $testRoot 'normal') } 'not declared shared' 'Admin-only module file'

 $extraFiles=@{}; foreach($name in $files.Keys) { $extraFiles[$name]=$files[$name] }; $extraFiles['server/game/mods/creative-gem-forges/extra.lua']=[byte[]](1,2,3)
 $extraArchive=Join-Path $testRoot 'extra.zip'; Write-TestArchive $extraArchive $extraFiles $manifest
 Assert-Rejected { Add-VerifiedSharedGemForgeToStage -ArchivePath $extraArchive -ExpectedSha256 (Get-FileHash -LiteralPath $extraArchive -Algorithm SHA256).Hash.ToLowerInvariant() -ExpectedSourceCommit $sourceCommit -StageDirectory (Join-Path $testRoot 'normal') } 'inventories differ' 'unlisted extra module file'

 $tamperedFiles=@{}; foreach($name in $files.Keys) { $tamperedFiles[$name]=$files[$name] }; $tamperedFiles['client/game/mods/creative-gem-forges/src/mod.lua']=[Text.UTF8Encoding]::new($false).GetBytes('tampered')
 $tamperedArchive=Join-Path $testRoot 'tampered.zip'; Write-TestArchive $tamperedArchive $tamperedFiles $manifest
 Assert-Rejected { Add-VerifiedSharedGemForgeToStage -ArchivePath $tamperedArchive -ExpectedSha256 (Get-FileHash -LiteralPath $tamperedArchive -Algorithm SHA256).Hash.ToLowerInvariant() -ExpectedSourceCommit $sourceCommit -StageDirectory (Join-Path $testRoot 'normal') } 'hash or size mismatch' 'bad module hash'

 $conflictOutput=Join-Path $testRoot 'prepared-conflict'
 New-Item -ItemType Directory -Path (Join-Path $conflictOutput 'mods/creative-gem-forges/src') -Force | Out-Null
 $conflictingFile=Join-Path $conflictOutput 'mods/creative-gem-forges/src/mod.lua'
 [IO.File]::WriteAllText($conflictingFile,'preserve pre-existing module file')
 '{}' | Set-Content -LiteralPath (Join-Path $conflictOutput 'prepared-files.json')
 $conflictingHash=Get-TestSha ([IO.File]::ReadAllBytes($conflictingFile))
 Assert-Rejected { Add-VerifiedSharedGemForgeToPreparedOutput -PackageRoot (Join-Path $testRoot 'cheeze') -OutputDirectory $conflictOutput -GemForgeMetadata $result } 'already contains Gem Forge files' 'prepared output collision'
 if((Get-TestSha ([IO.File]::ReadAllBytes($conflictingFile))) -cne $conflictingHash -or
    (Test-Path -LiteralPath (Join-Path $conflictOutput 'mods/creative-gem-forges/mod.json'))) { throw 'Prepared output collision changed or partially replaced existing files' }

 $tamperedPackage=Join-Path $testRoot 'tampered-package'
 New-Item -ItemType Directory -Path (Join-Path $tamperedPackage 'mods') -Force | Out-Null
 Copy-Item -LiteralPath (Join-Path $testRoot 'cheeze/mods/creative-gem-forges') -Destination (Join-Path $tamperedPackage 'mods') -Recurse
 [IO.File]::WriteAllText((Join-Path $tamperedPackage 'mods/creative-gem-forges/src/mod.lua'),'tampered package source')
 $tamperedOutput=Join-Path $testRoot 'prepared-tampered'
 New-Item -ItemType Directory -Path $tamperedOutput | Out-Null
 '{}' | Set-Content -LiteralPath (Join-Path $tamperedOutput 'prepared-files.json')
 Assert-Rejected { Add-VerifiedSharedGemForgeToPreparedOutput -PackageRoot $tamperedPackage -OutputDirectory $tamperedOutput -GemForgeMetadata $result } 'differs from its verified manifest' 'tampered prepared-output source'
 if(Test-Path -LiteralPath (Join-Path $tamperedOutput 'mods/creative-gem-forges')) { throw 'Rejected prepared-output source left partial candidate files' }

 Write-Host 'Gem Forge package and prepared-output contracts passed for normal and cheeze profiles.'
} finally {
 $resolvedTestRoot=[IO.Path]::GetFullPath($testRoot)
 if (!$resolvedTestRoot.StartsWith($temporaryRoot.TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Refusing to remove a test directory outside TEMP' }
 [GC]::Collect()
 [GC]::WaitForPendingFinalizers()
 Remove-Item -LiteralPath $resolvedTestRoot -Recurse -Force
}
