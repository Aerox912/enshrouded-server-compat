param(
 [string]$GemForgeCandidateArchive='',
 [string]$GemForgeCandidateSha256='',
 [string]$GemForgeCandidateCommit=''
)
$ErrorActionPreference='Stop'
$hasGemForgeInput=[bool]($GemForgeCandidateArchive -or $GemForgeCandidateSha256 -or $GemForgeCandidateCommit)
if($hasGemForgeInput -and (!$GemForgeCandidateArchive -or !$GemForgeCandidateSha256 -or !$GemForgeCandidateCommit)) { throw 'Gem Forge candidate archive, SHA-256, and source commit must be supplied together.' }
$sharedGemForgeHelper=Join-Path $PSScriptRoot 'SharedGemForge.psm1'
if($hasGemForgeInput) { Import-Module -Name $sharedGemForgeHelper -Force }
$repo=Split-Path -Parent $PSScriptRoot
$version=(Get-Content (Join-Path $repo 'version.txt') -Raw).Trim()
$pin=Get-Content (Join-Path $repo 'dependencies.lock.json') -Raw | ConvertFrom-Json
$cache=Join-Path $repo 'build/dependencies'; New-Item -ItemType Directory $cache -Force | Out-Null
$archive=Join-Path $cache 'patches.zip'
if(!(Test-Path $archive) -or (Get-FileHash $archive).Hash.ToLowerInvariant() -ne $pin.patches.sha256) { Invoke-WebRequest -Uri $pin.patches.url -OutFile $archive }
if((Get-FileHash $archive).Hash.ToLowerInvariant() -ne $pin.patches.sha256) { throw 'Maintained patch release checksum mismatch' }
if((Get-Item $archive).Length -ne $pin.patches.size) { throw 'Patch release size mismatch' }
$content=Join-Path $cache 'patches'; Expand-Archive -LiteralPath $archive -DestinationPath $content -Force
$provenance=Get-Content (Join-Path $content 'build-info.json') -Raw | ConvertFrom-Json
if($provenance.commit -ne $pin.patches.commit) { throw 'Patch source commit mismatch' }
$tool=Get-ChildItem $content -Filter PatchTool.exe -Recurse | Select-Object -First 1
$catalog=Get-ChildItem $content -Filter catalog.json -Recurse | Select-Object -First 1
$source=$catalog.Directory.FullName
foreach($profile in @('normal','cheeze')) {
 $stage=Join-Path $repo "build/package-$profile"; New-Item -ItemType Directory $stage -Force | Out-Null
 $gemForge=$null
 if($hasGemForgeInput) {
  $gemForge=Add-VerifiedSharedGemForgeToStage -ArchivePath $GemForgeCandidateArchive -ExpectedSha256 $GemForgeCandidateSha256 -ExpectedSourceCommit $GemForgeCandidateCommit -StageDirectory $stage
 } elseif(Test-Path -LiteralPath (Join-Path $stage 'mods/creative-gem-forges')) { throw "Stale Gem Forge module in package stage without a pinned candidate input: $stage" }
 Copy-Item -LiteralPath $tool.FullName -Destination $stage
 foreach($file in @('catalog.json','client-files.json','CREDITS.md','LICENSE','README.md')) { Copy-Item -LiteralPath (Join-Path $source $file) -Destination (Join-Path $stage ('PATCHES-'+$file)) }
 $catalogData=Get-Content (Join-Path $source 'catalog.json') -Raw | ConvertFrom-Json
 $adapterComponent=$catalogData.components | Where-Object id -EQ 'server-adapter'
 $adapterComponent.version=$version
 $artifact=@{url="https://github.com/Aerox912/enshrouded-server-compat/releases/download/v$version/server-compat-$version.dll";sha256=(Get-FileHash (Join-Path $repo "dist/server-compat-$version.dll")).Hash.ToLowerInvariant();size=(Get-Item (Join-Path $repo "dist/server-compat-$version.dll")).Length;commit=$(if($env:GITHUB_SHA){$env:GITHUB_SHA}else{git rev-parse HEAD})}
 $adapterComponent | Add-Member -NotePropertyName artifacts -NotePropertyValue @($artifact) -Force
 $catalogData | ConvertTo-Json -Depth 30 | Set-Content (Join-Path $stage 'catalog.json') -Encoding utf8
 Copy-Item -LiteralPath (Join-Path $source "profiles/$profile.json") -Destination (Join-Path $stage 'profile.json')
 Copy-Item -LiteralPath (Join-Path $source 'defaults') -Destination $stage -Recurse -Force
 New-Item -ItemType Directory (Join-Path $stage 'licenses') -Force | Out-Null
 Copy-Item -LiteralPath (Join-Path $source 'PYTHON-LICENSE.txt') -Destination (Join-Path $stage 'licenses')
 Copy-Item -LiteralPath (Join-Path $source 'PYINSTALLER-LICENSES') -Destination (Join-Path $stage 'licenses') -Recurse -Force
 foreach($file in @('LICENSE','CREDITS.md','INSTALL.md','tools/Prepare-Server.ps1','tools/Verify-Server.ps1')) { Copy-Item -LiteralPath (Join-Path $repo $file) -Destination $stage }
 if($gemForge) { Copy-Item -LiteralPath $sharedGemForgeHelper -Destination (Join-Path $stage 'SharedGemForge.psm1') -Force }
 Copy-Item -LiteralPath (Join-Path $repo 'vendor/minhook-1.3.4/LICENSE.txt') -Destination (Join-Path $stage 'licenses/MinHook.txt')
 $dependency=@{patchTool=@{sha256=(Get-FileHash $tool.FullName).Hash.ToLowerInvariant()};emm=$pin.emm}
 if($gemForge) { $dependency.gemForge=$gemForge }
 if($profile -eq 'cheeze') { Copy-Item -LiteralPath (Join-Path $repo "dist/server-compat-$version.dll") -Destination (Join-Path $stage 'adapter.dll');$dependency.adapter=@{sha256=(Get-FileHash (Join-Path $stage 'adapter.dll')).Hash.ToLowerInvariant()} }
 $dependency | ConvertTo-Json -Depth 8 | Set-Content (Join-Path $stage 'dependencies.lock.json') -Encoding utf8
 $commit=if($env:GITHUB_SHA){$env:GITHUB_SHA}else{git rev-parse HEAD}
 $buildInfo=@{schema=1;version=$version;profile=$profile;commit=$commit;dependencies=$pin;game='Dedicated server build 1024233';liveTest='Separate acceptance required'}
 if($gemForge) { $buildInfo.sharedGemForge=$gemForge }
 $buildInfo | ConvertTo-Json -Depth 12 | Set-Content (Join-Path $stage 'build-info.json') -Encoding utf8
 $files=Get-ChildItem $stage -Recurse -File
 if($files.Name -contains 'enshrouded_server.exe' -or $files.Extension -contains '.kfc' -or $files.Name -contains 'GlobalXPShare.original.dll') { throw 'Restricted server content in package' }
 Compress-Archive -Path "$stage/*" -DestinationPath (Join-Path $repo "dist/enshrouded-server-$profile-$version.zip") -Force
}
Get-ChildItem (Join-Path $repo 'dist') -File | Where-Object Extension -NE '.sha256' | ForEach-Object { "$((Get-FileHash $_.FullName).Hash.ToLowerInvariant())  $($_.Name)" | Set-Content ($_.FullName+'.sha256') -Encoding ascii }
