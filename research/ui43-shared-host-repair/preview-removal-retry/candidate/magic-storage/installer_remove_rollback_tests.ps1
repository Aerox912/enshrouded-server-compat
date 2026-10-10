[CmdletBinding()]
param(
 [Parameter(Mandatory=$true)][string]$FixtureRoot,
 [Parameter(Mandatory=$true)][string]$RunParent,
 [string]$Installer=(Join-Path $PSScriptRoot 'Install-UiPreview.ps1')
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$FixtureRoot=(Resolve-Path -LiteralPath $FixtureRoot).ProviderPath.TrimEnd('\')
$Installer=(Resolve-Path -LiteralPath $Installer).ProviderPath
$RunParent=[IO.Path]::GetFullPath($RunParent)
if(!(Test-Path -LiteralPath $FixtureRoot -PathType Container)){throw "Fixture missing: $FixtureRoot"}
if(!(Test-Path -LiteralPath $Installer -PathType Leaf)){throw "Installer missing: $Installer"}
New-Item -ItemType Directory -Path $RunParent -Force|Out-Null
$RunRoot=Join-Path $RunParent (([DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ'))+'-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $RunRoot|Out-Null
$script:Installer=$Installer;$global:Install157MoveCall=0;$global:Install157MoveFaultCalls=@()
function Assert([bool]$Condition,[string]$Message){if(!$Condition){throw $Message}}
function Get-CimInstance {
 [CmdletBinding()] param([Parameter(Position=0)][string]$ClassName,[string]$Filter)
 return $null
}
function Move-Item {
 [CmdletBinding()] param([Parameter(Mandatory=$true)][string]$LiteralPath,[Parameter(Mandatory=$true)][string]$Destination)
 $global:Install157MoveCall++
 if($global:Install157MoveFaultCalls -contains $global:Install157MoveCall){
  $failedCall=$global:Install157MoveCall
  $global:Install157MoveFaultCalls=@($global:Install157MoveFaultCalls|Where-Object{$_ -ne $failedCall})
  throw "INSTALL157 injected Move-Item failure at call $failedCall"
 }
 Microsoft.PowerShell.Management\Move-Item -LiteralPath $LiteralPath -Destination $Destination -ErrorAction Stop
}
function Get-TreeSnapshot([string]$Root){
 $rootFull=[IO.Path]::GetFullPath($Root).TrimEnd('\');$snapshot=[ordered]@{}
 foreach($item in Get-ChildItem -LiteralPath $rootFull -Force -Recurse|Sort-Object FullName){
  if($item.Attributes -band [IO.FileAttributes]::ReparsePoint){throw "Unexpected reparse point in test clone: $($item.FullName)"}
  $relative=$item.FullName.Substring($rootFull.Length+1).Replace('\','/')
  if($item.PSIsContainer){$snapshot["d:$relative"]=''}
  else{$snapshot["f:$relative"]=(Get-FileHash -LiteralPath $item.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}
 }
 return $snapshot
}
function Assert-SameSnapshot($Before,$After,[string]$CaseName){
 $a=ConvertTo-Json -InputObject $Before -Compress -Depth 10
 $b=ConvertTo-Json -InputObject $After -Compress -Depth 10
 Assert ($a -ceq $b) "$($CaseName): filesystem snapshot changed across failed transaction."
}
function New-TestCase([string]$Name){
 $caseRoot=Join-Path $RunRoot $Name
 New-Item -ItemType Directory -Path $caseRoot|Out-Null
 foreach($entry in Get-ChildItem -LiteralPath $FixtureRoot -Force){Copy-Item -LiteralPath $entry.FullName -Destination $caseRoot -Recurse -Force}
 $removed=Join-Path $caseRoot '.magic-storage-ui-backups\fixture\removed'
 Assert (Test-Path -LiteralPath $removed -PathType Container) "$($Name): expected stale fixture marker."
 Assert (@(Get-ChildItem -LiteralPath $removed -Force -Recurse).Count -eq 0) "$($Name): fixture marker must be empty."
 Remove-Item -LiteralPath $removed -Force -ErrorAction Stop
 $receiptPath=Join-Path $caseRoot 'mods\magic_storage\install-receipt.json'
 $receipt=Get-Content -LiteralPath $receiptPath -Raw|ConvertFrom-Json
 $receipt.game=$caseRoot
 $receipt.backup=Join-Path $caseRoot '.magic-storage-ui-backups\fixture'
 [IO.File]::WriteAllText($receiptPath,(($receipt|ConvertTo-Json -Depth 100)+[Environment]::NewLine),[Text.UTF8Encoding]::new($false))
 return $caseRoot
}
function Add-SyntheticOriginals([string]$CaseRoot){
 $receiptPath=Join-Path $CaseRoot 'mods\magic_storage\install-receipt.json'
 $receipt=Get-Content -LiteralPath $receiptPath -Raw|ConvertFrom-Json
 $originals=Join-Path $CaseRoot '.magic-storage-ui-backups\fixture\originals'
 $installedCreative=Join-Path $CaseRoot 'mods\creative_mode';$originalCreative=Join-Path $originals 'creative_mode'
 New-Item -ItemType Directory -Path $installedCreative,$originalCreative|Out-Null
 [IO.File]::WriteAllBytes((Join-Path $installedCreative 'creative_mode.dll'),[Text.Encoding]::UTF8.GetBytes('synthetic-installed-creative'))
 [IO.File]::WriteAllBytes((Join-Path $originalCreative 'creative_mode.dll'),[Text.Encoding]::UTF8.GetBytes('synthetic-original-creative'))
 $installedProbe=Join-Path $CaseRoot 'mods\magic_storage_probe';$originalProbe=Join-Path $originals 'magic_storage_probe'
 New-Item -ItemType Directory -Path $installedProbe,$originalProbe|Out-Null
 [IO.File]::WriteAllBytes((Join-Path $installedProbe 'magic_storage_probe.dll'),[Text.Encoding]::UTF8.GetBytes('synthetic-installed-probe'))
 [IO.File]::WriteAllBytes((Join-Path $originalProbe 'magic_storage_probe.dll'),[Text.Encoding]::UTF8.GetBytes('synthetic-original-probe'))
 $receipt.installed.creative_mode=[pscustomobject]@{'creative_mode.dll'=(Get-FileHash -LiteralPath (Join-Path $installedCreative 'creative_mode.dll') -Algorithm SHA256).Hash.ToLowerInvariant()}
 $receipt.original.creative_mode.exists=$true
 $receipt.original.creative_mode.files=[pscustomobject]@{'creative_mode.dll'=(Get-FileHash -LiteralPath (Join-Path $originalCreative 'creative_mode.dll') -Algorithm SHA256).Hash.ToLowerInvariant()}
 $receipt.installed.magic_storage_probe=[pscustomobject]@{'magic_storage_probe.dll'=(Get-FileHash -LiteralPath (Join-Path $installedProbe 'magic_storage_probe.dll') -Algorithm SHA256).Hash.ToLowerInvariant()}
 $receipt.original.magic_storage_probe.exists=$true
 $receipt.original.magic_storage_probe.files=[pscustomobject]@{'magic_storage_probe.dll'=(Get-FileHash -LiteralPath (Join-Path $originalProbe 'magic_storage_probe.dll') -Algorithm SHA256).Hash.ToLowerInvariant()}
 [IO.File]::WriteAllText($receiptPath,(($receipt|ConvertTo-Json -Depth 100)+[Environment]::NewLine),[Text.UTF8Encoding]::new($false))
}
function Invoke-Remove([string]$CaseRoot,[int[]]$FaultCalls=@()){
 $global:Install157MoveCall=0;$global:Install157MoveFaultCalls=@($FaultCalls)
 try{
  $output=@(& $script:Installer -Action Remove -GameDirectory $CaseRoot -Confirm:$false -ErrorAction Stop)
  return [pscustomobject]@{succeeded=$true;output=([string]::Join([Environment]::NewLine,@($output)));error=$null;moveCalls=$global:Install157MoveCall}
 }catch{return [pscustomobject]@{succeeded=$false;output=$null;error=$_.Exception.Message;moveCalls=$global:Install157MoveCall}}
 finally{$global:Install157MoveFaultCalls=@()}
}
function Get-ConfigHash([string]$CaseRoot){
 return (Get-FileHash -LiteralPath (Join-Path $CaseRoot 'shroudtopia.json') -Algorithm SHA256).Hash.ToLowerInvariant()
}
function Assert-ConfigUnchanged([string]$CaseRoot,[string]$Expected,[string]$CaseName){
 $current=Get-ConfigHash $CaseRoot
 Assert ($current -ceq $Expected) "$($CaseName): configuration changed during failed transaction."
}
function Assert-ConfigRestored([string]$CaseRoot,[string]$CaseName){
 $current=Get-ConfigHash $CaseRoot
 $original=(Get-FileHash -LiteralPath (Join-Path $CaseRoot '.magic-storage-ui-backups\fixture\shroudtopia.before.json') -Algorithm SHA256).Hash.ToLowerInvariant()
 Assert ($current -ceq $original) "$($CaseName): configuration bytes were not restored."
}
$first=New-TestCase 'first-move';$firstBefore=Get-TreeSnapshot $first;$firstConfigBefore=Get-ConfigHash $first
$firstFailure=Invoke-Remove $first @(1)
Assert (!$firstFailure.succeeded) 'First-move injection unexpectedly succeeded.'
Assert ($firstFailure.error -match 'INSTALL157 injected Move-Item failure at call 1') "First-move failure did not propagate: $($firstFailure.error)"
Assert (!(Test-Path -LiteralPath (Join-Path $first '.magic-storage-ui-backups\fixture\removed'))) 'Failed first move left a marker.'
Assert-SameSnapshot $firstBefore (Get-TreeSnapshot $first) 'first-move';Assert-ConfigUnchanged $first $firstConfigBefore 'first-move'
$firstRetry=Invoke-Remove $first
Assert ($firstRetry.succeeded) "First-move retry failed: $($firstRetry.error)"
Assert (!(Test-Path -LiteralPath (Join-Path $first 'mods\magic_storage'))) 'First-move retry did not remove the preview package.'
Assert (Test-Path -LiteralPath (Join-Path $first '.magic-storage-ui-backups\fixture\removed\magic_storage')) 'First-move retry did not retain the preview package.'
Assert-ConfigRestored $first 'first-move retry'
$mid=New-TestCase 'mid-transaction';Add-SyntheticOriginals $mid;$midBefore=Get-TreeSnapshot $mid;$midConfigBefore=Get-ConfigHash $mid
$midFailure=Invoke-Remove $mid @(4)
Assert (!$midFailure.succeeded) 'Mid-transaction injection unexpectedly succeeded.'
Assert ($midFailure.error -match 'INSTALL157 injected Move-Item failure at call 4') 'Mid-transaction failure did not propagate.'
Assert (!(Test-Path -LiteralPath (Join-Path $mid '.magic-storage-ui-backups\fixture\removed'))) 'Mid-transaction rollback left a marker.'
Assert-SameSnapshot $midBefore (Get-TreeSnapshot $mid) 'mid-transaction';Assert-ConfigUnchanged $mid $midConfigBefore 'mid-transaction'
$midRetry=Invoke-Remove $mid
Assert ($midRetry.succeeded) "Mid-transaction retry failed: $($midRetry.error)"
Assert (Test-Path -LiteralPath (Join-Path $mid 'mods\creative_mode\creative_mode.dll')) 'Mid-transaction retry did not restore original Creative.'
Assert (Test-Path -LiteralPath (Join-Path $mid 'mods\magic_storage_probe\magic_storage_probe.dll')) 'Mid-transaction retry did not restore original probe.'
Assert (Test-Path -LiteralPath (Join-Path $mid '.magic-storage-ui-backups\fixture\removed\creative_mode\creative_mode.dll')) 'Mid-transaction retry did not retain installed Creative.'
Assert (Test-Path -LiteralPath (Join-Path $mid '.magic-storage-ui-backups\fixture\removed\magic_storage_probe\magic_storage_probe.dll')) 'Mid-transaction retry did not retain installed probe.'
Assert-ConfigRestored $mid 'mid-transaction retry'
$config=New-TestCase 'config-replace';$configBefore=Get-TreeSnapshot $config;$configConfigBefore=Get-ConfigHash $config;$configPath=Join-Path $config 'shroudtopia.json'
$configHandle=$null;$configFailure=$null
try{$configHandle=[IO.File]::Open($configPath,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read);$configFailure=Invoke-Remove $config}
finally{if($configHandle){$configHandle.Dispose()}}
Assert (!$configFailure.succeeded) 'Config replacement unexpectedly succeeded with a delete-sharing lock.'
Assert ($configFailure.moveCalls -ge 2) 'Config replacement fault did not occur after package movement.'
Assert (!(Test-Path -LiteralPath (Join-Path $config '.magic-storage-ui-backups\fixture\removed'))) 'Config rollback left a marker.'
Assert-SameSnapshot $configBefore (Get-TreeSnapshot $config) 'config-replace';Assert-ConfigUnchanged $config $configConfigBefore 'config-replace'
$configRetry=Invoke-Remove $config
Assert ($configRetry.succeeded) "Config replacement retry failed: $($configRetry.error)";Assert-ConfigRestored $config 'config-replace retry'
$rollback=New-TestCase 'rollback-failure';Add-SyntheticOriginals $rollback
$rollbackFailure=Invoke-Remove $rollback @(4,5)
Assert (!$rollbackFailure.succeeded) 'Rollback-failure injection unexpectedly succeeded.'
Assert ($rollbackFailure.error -match 'Preview removal failed:') 'Initiating transaction failure was not reported.'
Assert ($rollbackFailure.error -match 'Rollback also failed:') 'Rollback failure was masked.'
$rollbackMarker=Join-Path $rollback '.magic-storage-ui-backups\fixture\removed'
Assert (Test-Path -LiteralPath $rollbackMarker -PathType Container) 'Rollback failure did not preserve its marker.'
Assert (@(Get-ChildItem -LiteralPath $rollbackMarker -Force -Recurse -File).Count -gt 0) 'Rollback failure did not retain recovery data.'
Assert (Test-Path -LiteralPath (Join-Path $rollback 'mods\creative_mode\creative_mode.dll')) 'Rollback failure lost the original Creative payload.'
$summary=[pscustomobject]@{
 taskId='INSTALL157';result='PASS';fixture=$FixtureRoot;runRoot=$RunRoot
 cases=@(
  [pscustomobject]@{name='first-move';injectedCall=1;rollbackExactTree=$true;successfulRetry=$true}
  [pscustomobject]@{name='mid-transaction';injectedCall=4;rollbackExactTree=$true;successfulRetry=$true}
  [pscustomobject]@{name='config-replace';mechanism='deny FILE_SHARE_DELETE';rollbackExactTree=$true;successfulRetry=$true}
  [pscustomobject]@{name='rollback-failure';injectedCalls=@(4,5);bothErrorsReported=$true;recoveryDataRetained=$true}
 )
}
$summaryPath=Join-Path $RunRoot 'install157-focused-test-results.json'
[IO.File]::WriteAllText($summaryPath,(($summary|ConvertTo-Json -Depth 20)+[Environment]::NewLine),[Text.UTF8Encoding]::new($false))
$summary|ConvertTo-Json -Depth 20
Write-Output "Evidence: $summaryPath"
