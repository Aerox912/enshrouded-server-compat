param([string]$OutputDirectory='E:\Build\g-install10-20261009',
      [string]$CreativeRoot='E:\Worktrees\enshrouded-creative-parity-20261009')
$ErrorActionPreference='Stop'
$root=(Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$vcvars='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
# Obtain the native toolchain environment without changing execution policy.
$environment=& $env:ComSpec /d /s /c "`"$vcvars`" >nul && set"
if($LASTEXITCODE -ne 0){throw 'vcvars64 failed'}
foreach($line in $environment){if($line -match '^([^=]+)=(.*)$'){[Environment]::SetEnvironmentVariable($matches[1],$matches[2],'Process')}}
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
Push-Location $OutputDirectory
try{
  & cl.exe /nologo /std:c++20 /W4 /WX /EHsc /MT /O2 /DWIN32_LEAN_AND_MEAN /DNOMINMAX "/I$root\src" /c "$root\src\creative_flight_call_transaction.cpp" /Fo:creative_flight_call_production.obj
  if($LASTEXITCODE -ne 0){throw 'production compile failed'}
  $symbols=& dumpbin.exe /nologo /symbols creative_flight_call_production.obj
  if($LASTEXITCODE -ne 0 -or ($symbols | Select-String -Pattern 'publish_native_fixture|pin_native_fixture_bridge|native_thread_snapshot|reject_fixture_file_identity|native_atomic_probe|isolated_test')){throw 'test override leaked into production object'}
  & cl.exe /nologo /std:c++20 /W4 /WX /EHsc /MT /O2 /DCREATIVE_CALL_INSTALL_SELFTEST /DWIN32_LEAN_AND_MEAN /DNOMINMAX "/I$root\src" /c "$root\src\creative_flight_call_transaction.cpp"
  if($LASTEXITCODE -ne 0){throw 'transaction compile failed'}
  & cl.exe /nologo /std:c++20 /W4 /WX /EHsc /MT /O2 /DCREATIVE_CALL_INSTALL_SELFTEST /DWIN32_LEAN_AND_MEAN /DNOMINMAX "/I$root\src" "$root\tests\creative_call_installer\transaction_tests.cpp" creative_flight_call_transaction.obj bcrypt.lib /Fe:transaction_tests.exe
  if($LASTEXITCODE -ne 0){throw 'transaction tests build failed'}
  & ml64.exe /nologo /c "$root\src\creative_flight_dispatch_bridge.asm" "$root\tests\creative_call_installer\native_fixture.asm"
  if($LASTEXITCODE -ne 0){throw 'native fixture assembly failed'}
  & cl.exe /nologo /std:c++20 /W4 /WX /EHsc /MT /O2 /LD "$root\tests\creative_call_installer\bridge_dll_fixture.cpp" creative_flight_dispatch_bridge.obj /Fe:bridge_dll_fixture.dll /link /EXPORT:creative_g_dispatch_bridge
  if($LASTEXITCODE -ne 0){throw 'pin fixture DLL build failed'}
  & cl.exe /nologo /std:c++20 /W4 /WX /EHsc /MT /O2 /DCREATIVE_CALL_INSTALL_SELFTEST /DWIN32_LEAN_AND_MEAN /DNOMINMAX "/I$root\src" "$root\tests\creative_call_installer\native_tests.cpp" creative_flight_call_transaction.obj creative_flight_dispatch_bridge.obj native_fixture.obj bcrypt.lib /Fe:native_tests.exe
  if($LASTEXITCODE -ne 0){throw 'native tests build failed'}
  & cl.exe /nologo /std:c++20 /W4 /WX /EHsc /MT /O2 /DWIN32_LEAN_AND_MEAN /DNOMINMAX "/I$root\src" /c "$root\src\creative_flight_call_service.cpp" /Fo:creative_flight_call_service_production.obj
  if($LASTEXITCODE -ne 0){throw 'service production compile failed'}
  $serviceSymbols=& dumpbin.exe /nologo /symbols creative_flight_call_service_production.obj
  if($LASTEXITCODE -ne 0 -or ($serviceSymbols | Select-String -Pattern 'start_test|test_install_|test_recover_')){throw 'test injection leaked into production service'}
  & cl.exe /nologo /std:c++20 /W4 /WX /EHsc /MT /O2 /DWIN32_LEAN_AND_MEAN /DNOMINMAX "/I$root\src" "$root\tests\creative_call_installer\production_service_tests.cpp" creative_flight_call_service_production.obj creative_flight_call_production.obj bcrypt.lib /Fe:production_service_tests.exe
  if($LASTEXITCODE -ne 0){throw 'production service tests build failed'}
  & .\production_service_tests.exe
  if($LASTEXITCODE -ne 0){throw 'production service tests failed'}
  & cl.exe /nologo /std:c++20 /W4 /WX /EHsc /MT /O2 /DCREATIVE_CALL_INSTALL_SELFTEST /DWIN32_LEAN_AND_MEAN /DNOMINMAX "/I$root\src" "/I$creativeRoot\include" "$root\tests\creative_call_installer\integration_tests.cpp" "$root\src\creative_flight_call_service.cpp" "$root\src\flight_session.cpp" creative_flight_call_transaction.obj bcrypt.lib /Fe:integration_tests.exe
  if($LASTEXITCODE -ne 0){throw 'integration seam tests build failed'}
  & .\integration_tests.exe
  if($LASTEXITCODE -ne 0){throw 'integration seam tests failed'}
  & .\transaction_tests.exe
  if($LASTEXITCODE -ne 0){throw 'transaction tests failed'}
  & .\native_tests.exe
  if($LASTEXITCODE -ne 0){throw 'native tests failed'}
}finally{Pop-Location}
