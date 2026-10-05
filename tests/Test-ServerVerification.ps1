$ErrorActionPreference='Stop'
$source=Split-Path -Parent $PSScriptRoot
$fixture=Join-Path $source ('build/verify-'+[Guid]::NewGuid().ToString('N'))
$game=Join-Path $fixture 'game'
New-Item -ItemType Directory $game -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $source 'tools/Verify-Server.ps1') -Destination $fixture
[IO.File]::WriteAllText((Join-Path $game 'enshrouded_server.exe'),'fixture executable')
$gameHash=(Get-FileHash (Join-Path $game 'enshrouded_server.exe')).Hash.ToLowerInvariant()
[IO.File]::WriteAllText((Join-Path $game 'xp-original.dll'),'fixture upstream XP')
$xpHash=(Get-FileHash (Join-Path $game 'xp-original.dll')).Hash.ToLowerInvariant()
$xpConfig="EnableXPShare=1`nShareMultiplier=1.0`n"
[IO.File]::WriteAllText((Join-Path $game 'safeprobe_config.ini'),$xpConfig)
@{game=@{serverSha256=$gameHash};components=@(@{id='global-xp';files=@(@{sha256=$xpHash})})} | ConvertTo-Json -Depth 10 | Set-Content (Join-Path $fixture 'catalog.json')
'{}' | Set-Content (Join-Path $game 'prepared-files.json')
'{}' | Set-Content (Join-Path $fixture 'dependencies.lock.json')
function Must-Reject([scriptblock]$Action,[string]$Expected) {
 $caught=$false
 try { & $Action } catch { if($_.Exception.Message -notlike "*$Expected*"){throw};$caught=$true }
 if(!$caught){throw "Expected rejection: $Expected"}
}
foreach($profile in @('normal','cheeze')) {
 @{id=$profile} | ConvertTo-Json | Set-Content (Join-Path $fixture 'profile.json')
 if($profile -eq 'normal') { Copy-Item (Join-Path $game 'xp-original.dll') (Join-Path $game 'dbghelp.dll') }
 else {
  Copy-Item (Join-Path $game 'xp-original.dll') (Join-Path $game 'GlobalXPShare.original.dll')
  [IO.File]::WriteAllText((Join-Path $game 'dbghelp.dll'),'fixture adapter')
  @{adapter=@{sha256=(Get-FileHash (Join-Path $game 'dbghelp.dll')).Hash.ToLowerInvariant()}} | ConvertTo-Json -Depth 5 | Set-Content (Join-Path $fixture 'dependencies.lock.json')
 }
 & (Join-Path $fixture 'Verify-Server.ps1') -ServerDirectory $game
 [IO.File]::WriteAllText((Join-Path $game 'safeprobe_config.ini'),"EnableXPShare=1`nShareMultiplier=2.0`n")
 Must-Reject { & (Join-Path $fixture 'Verify-Server.ps1') -ServerDirectory $game } 'expected enabled 1x'
 [IO.File]::WriteAllText((Join-Path $game 'safeprobe_config.ini'),$xpConfig)
 [IO.File]::AppendAllText((Join-Path $game 'dbghelp.dll'),'changed')
 Must-Reject { & (Join-Path $fixture 'Verify-Server.ps1') -ServerDirectory $game } $(if($profile -eq 'normal'){'upstream XP'}else{'chain differs'})
}
Write-Host '6 server verification acceptance/rejection checks passed.'
