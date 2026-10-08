param(
    [string]$GameDirectory = 'D:\Games\SteamLibrary\steamapps\common\Enshrouded',
    [string]$BackupDirectory = 'E:\Build\enshrouded-flight-server\client-test-backup-20261008'
)
$ErrorActionPreference = 'Stop'
if (Get-Process -Name enshrouded -ErrorAction SilentlyContinue) { throw 'Close Enshrouded before restoring the client.' }
$target = Join-Path $GameDirectory 'mods\flight_mod\flight_mod.dll'
$backup = Join-Path $BackupDirectory 'flight_mod.dll'
$original = '4F66CB115F2C67ED9FD603E7CAD3B70FCE348937D99058B1236D9F39BEE44DBD'
$development = 'F0F81B886F62386D555C0EC2704ACBAD5AC1D2205DF8D731035673EE50B3CFD3'
if ((Get-FileHash -LiteralPath $backup).Hash -ne $original) { throw 'Backup hash mismatch; nothing restored.' }
$installed = (Get-FileHash -LiteralPath $target).Hash
if ($installed -eq $original) { Write-Output 'Original flight client is already restored.'; return }
if ($installed -ne $development) { throw 'Installed DLL changed since the test; refusing to overwrite another update.' }
Copy-Item -LiteralPath $backup -Destination $target
if ((Get-FileHash -LiteralPath $target).Hash -ne $original) { throw 'Restore verification failed.' }
Write-Output 'Original flight client restored. User settings were preserved.'
