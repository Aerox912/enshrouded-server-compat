param([Parameter(Mandatory)][string]$ServerDirectory,[switch]$CheckStartupLogs)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath($ServerDirectory).TrimEnd('\','/')
$profile=Get-Content (Join-Path $PSScriptRoot 'profile.json') -Raw | ConvertFrom-Json
$catalog=Get-Content (Join-Path $PSScriptRoot 'catalog.json') -Raw | ConvertFrom-Json
$lock=Get-Content (Join-Path $PSScriptRoot 'dependencies.lock.json') -Raw | ConvertFrom-Json
function File-Hash($Relative) {
 $path=[IO.Path]::GetFullPath((Join-Path $root $Relative))
 if(!$path.StartsWith($root+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) { throw 'Verification path escaped the server directory' }
 if(!(Test-Path -LiteralPath $path -PathType Leaf)) { throw "Missing server file: $Relative" }
 return (Get-FileHash -LiteralPath $path).Hash.ToLowerInvariant()
}
if((File-Hash 'enshrouded_server.exe') -ne $catalog.game.serverSha256) { throw 'Unsupported server executable' }
$manifest=Get-Content -LiteralPath (Join-Path $root 'prepared-files.json') -Raw | ConvertFrom-Json
foreach($file in $manifest.PSObject.Properties) {
 if((File-Hash $file.Name) -ne $file.Value.sha256) { throw "Prepared file differs: $($file.Name)" }
}
$xp=($catalog.components | Where-Object id -EQ 'global-xp').files[0].sha256
if($profile.id -eq 'normal') {
 if((File-Hash 'dbghelp.dll') -ne $xp) { throw 'Normal must keep the upstream XP Share loader' }
 if(Test-Path -LiteralPath (Join-Path $root 'GlobalXPShare.original.dll')) { throw 'Unexpected Cheeze XP chain in the Normal profile' }
} else {
 if((File-Hash 'dbghelp.dll') -ne $lock.adapter.sha256 -or (File-Hash 'GlobalXPShare.original.dll') -ne $xp) { throw 'Cheeze adapter/XP chain differs from this package' }
}
$xpConfig=Get-Content -LiteralPath (Join-Path $root 'safeprobe_config.ini') -Raw
if($xpConfig -notmatch '(?m)^\s*EnableXPShare\s*=\s*1\s*$' -or $xpConfig -notmatch '(?m)^\s*ShareMultiplier\s*=\s*1(?:\.0)?\s*$') { throw 'XP Share is not configured for the expected enabled 1x setting' }
if($CheckStartupLogs) {
 $serverLog=Get-Content -LiteralPath (Join-Path $root 'logs/enshrouded_server.log') -Raw
 if($serverLog -notmatch 'Host_Online') { throw 'No Host_Online marker in the supplied server log' }
 $xpLog=Get-Content -LiteralPath (Join-Path $root 'xp_validation_log.txt') -Raw
 if($xpLog -notmatch 'HOOK INSTALLED') { throw 'No XP hook installation marker in the supplied XP log' }
 if($profile.id -eq 'cheeze') {
  $adapterLog=Get-Content -LiteralPath (Join-Path $root 'xhl-server-adapter.log') -Raw
  if($adapterLog -notmatch 'HOOKS INSTALLED: 5' -or $adapterLog -notmatch 'EXTRAS HOOKS INSTALLED') { throw 'Required Cheeze hook markers are missing' }
 }
}
Write-Host "Verified $($profile.id) files, XP chain and settings. Wine still requires dbghelp=n,b. Check fresh startup logs and test XP transfer between two registered players; log markers alone do not prove gameplay."
