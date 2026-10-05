$ErrorActionPreference='Stop'
Set-Location (Split-Path -Parent $PSScriptRoot)
cmake -S . -B build -A x64 -T v143
if($LASTEXITCODE) { throw 'CMake configure failed' }
cmake --build build --config Release --parallel
if($LASTEXITCODE) { throw 'Native build failed' }
ctest --test-dir build -C Release --output-on-failure
if($LASTEXITCODE) { throw 'Cloud native checks failed' }
New-Item -ItemType Directory dist -Force | Out-Null
$version=(Get-Content ./version.txt -Raw).Trim()
Copy-Item build/Release/dbghelp.dll "dist/server-compat-$version.dll"
# Package assembly consumes only the verified maintained patch release.
./ci/package.ps1
