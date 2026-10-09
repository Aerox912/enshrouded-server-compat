param(
 [Parameter(Mandatory)][string]$Originals,
 [Parameter(Mandatory)][string]$OutputDirectory,
 [string]$StagingGameDirectory='',
 [string]$ExistingServerConfig=''
)
$ErrorActionPreference='Stop'
$profile=Get-Content (Join-Path $PSScriptRoot 'profile.json') -Raw | ConvertFrom-Json
$catalog=Get-Content (Join-Path $PSScriptRoot 'catalog.json') -Raw | ConvertFrom-Json
$lock=Get-Content (Join-Path $PSScriptRoot 'dependencies.lock.json') -Raw | ConvertFrom-Json
function Verify-File($Path,$Hash) {
 if(!(Test-Path -LiteralPath $Path -PathType Leaf) -or (Get-FileHash -LiteralPath $Path).Hash.ToLowerInvariant() -ne $Hash) { throw "Checksum mismatch: $Path" }
}
Verify-File (Join-Path $PSScriptRoot 'PatchTool.exe') $lock.patchTool.sha256
if($profile.id -eq 'cheeze') { Verify-File (Join-Path $PSScriptRoot 'adapter.dll') $lock.adapter.sha256 }
if(Test-Path -LiteralPath $OutputDirectory) { throw 'Choose a new output directory; existing files are preserved.' }
if($StagingGameDirectory) {
 if(Get-Process enshrouded_server -ErrorAction SilentlyContinue) { throw 'Close the dedicated server before preparing resources.' }
 Verify-File (Join-Path $StagingGameDirectory 'enshrouded_server.exe') $catalog.game.serverSha256
 if(Test-Path -LiteralPath (Join-Path $StagingGameDirectory 'enshrouded_server.kfc.bak')) { throw 'Use clean staging game data, not already-patched resources.' }
 if(Test-Path -LiteralPath (Join-Path $StagingGameDirectory 'mods')) { throw 'Use staging without existing mods to prevent doubled multipliers.' }
}
$arguments=@('server',$profile.id,[IO.Path]::GetFullPath($Originals),[IO.Path]::GetFullPath($OutputDirectory))
if($profile.id -eq 'cheeze') { $arguments+=@('--adapter',(Join-Path $PSScriptRoot 'adapter.dll')) }
& (Join-Path $PSScriptRoot 'PatchTool.exe') @arguments
if($LASTEXITCODE) { throw 'Original import failed. No live installation was changed.' }
$packageRoot=$PSScriptRoot
if(!(Test-Path -LiteralPath (Join-Path $packageRoot 'profile.json') -PathType Leaf)) { $packageRoot=Split-Path -Parent $PSScriptRoot }
if($lock.PSObject.Properties['gemForge']) {
 $sharedGemForgeHelper=Join-Path $PSScriptRoot 'SharedGemForge.psm1'
 if(!(Test-Path -LiteralPath $sharedGemForgeHelper -PathType Leaf)) { $sharedGemForgeHelper=Join-Path (Split-Path -Parent $PSScriptRoot) 'ci/SharedGemForge.psm1' }
 if(!(Test-Path -LiteralPath $sharedGemForgeHelper -PathType Leaf)) { throw 'Pinned Gem Forge package has no verifier module.' }
 Import-Module -Name $sharedGemForgeHelper -Force
 Add-VerifiedSharedGemForgeToPreparedOutput -PackageRoot $packageRoot -OutputDirectory $OutputDirectory -GemForgeMetadata $lock.gemForge | Out-Null
} elseif(Test-Path -LiteralPath (Join-Path $packageRoot 'mods/creative-gem-forges')) {
 throw 'Stale Gem Forge module files are present without a pinned dependencies.lock.json entry.'
}
if($ExistingServerConfig) {
 $config=Get-Content -LiteralPath $ExistingServerConfig -Raw | ConvertFrom-Json
 foreach($p in $profile.serverSettings.PSObject.Properties) {
  if($p.Name -eq 'gameSettings') {
   if(!$config.PSObject.Properties['gameSettings']) { $config | Add-Member -NotePropertyName gameSettings -NotePropertyValue ([pscustomobject]@{}) }
   foreach($setting in $p.Value.PSObject.Properties) { $config.gameSettings | Add-Member -NotePropertyName $setting.Name -NotePropertyValue $setting.Value -Force }
  } else { $config | Add-Member -NotePropertyName $p.Name -NotePropertyValue $p.Value -Force }
 }
 $config | ConvertTo-Json -Depth 50 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'enshrouded_server.json') -Encoding utf8
}
if($StagingGameDirectory) {
 $cache=Join-Path $env:LOCALAPPDATA 'EnshroudedServerMods/downloads'
 New-Item -ItemType Directory $cache -Force | Out-Null
 $emm=Join-Path $cache ($lock.emm.sha256+'.exe')
 if(!(Test-Path -LiteralPath $emm)) { Invoke-WebRequest -Uri $lock.emm.url -OutFile ($emm+'.partial'); Verify-File ($emm+'.partial') $lock.emm.sha256; Move-Item -LiteralPath ($emm+'.partial') -Destination $emm }
 Verify-File $emm $lock.emm.sha256
 Get-ChildItem -LiteralPath $OutputDirectory | Copy-Item -Destination $StagingGameDirectory -Recurse
 & $emm run --patch --force -g $StagingGameDirectory
 if($LASTEXITCODE) { throw 'EMM patching failed. Discard this staging copy; no live server was changed.' }
}
Write-Host 'Prepared locally. Review INSTALL.md before manual server deployment; keep the existing XP configuration and world backup.'
