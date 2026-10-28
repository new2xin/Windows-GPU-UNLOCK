param(
 [string]$Assets=(Join-Path $PSScriptRoot 'build/firmware-20261026_210934_481/assets-py312'),
 [string]$Capture=(Join-Path $PSScriptRoot 'results/20261026_215558_245/GPU-PORT/RUN002'),
 [string]$Clang='C:/Program Files/LLVM/bin/clang.exe',
 [string]$VcVars='C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Auxiliary/Build/vcvars64.bat'
)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$out=Join-Path $PSScriptRoot ('results/layout-'+(Get-Date -Format yyyyMMdd_HHmmss_fff))
& python (Join-Path $PSScriptRoot 'make_stock_fixture.py') $Capture $out
if($LASTEXITCODE -ne 0){throw 'Authenticated v4 fixture generation failed'}
$Prom=Join-Path $Capture 'PROM.BIN'
$environment=& cmd.exe /d /c "call `"$VcVars`" >nul && set"
if($LASTEXITCODE -ne 0){throw 'MSVC environment failed'}
foreach($line in $environment){if($line -match '^([^=]+)=(.*)$'){[Environment]::SetEnvironmentVariable($matches[1],$matches[2],'Process')}}
$inc=Join-Path $repo 'deps/edk2/MdePkg/Include'
$sources=@((Join-Path $PSScriptRoot 'inspect_layout.c'),(Join-Path $repo 'internal-boot/unlock/sha256.c'),(Join-Path $repo 'internal-boot/unlock/vendor/prep_610.c'),(Join-Path $repo 'internal-boot/unlock/vendor/layout_610.c'),(Join-Path $repo 'internal-boot/unlock/vendor/vbios_610.c'))
& $Clang -std=c11 -O1 -Wall -Wextra -Werror -D_CRT_SECURE_NO_WARNINGS -I $out -I $inc -I ($inc+'/X64') @sources -o (Join-Path $out 'inspect-layout.exe')
if($LASTEXITCODE -ne 0){throw 'Production parser/layout compile failed'}
$json=& (Join-Path $out 'inspect-layout.exe') $Prom $Assets 2> (Join-Path $out 'parser.log')
if($LASTEXITCODE -ne 0){throw 'Production parser/layout validation failed'}
$json | ConvertFrom-Json | Out-Null
$json | Set-Content (Join-Path $out 'layout.json') -Encoding ascii
$json
Write-Output ('LAYOUT_DIR='+$out)
