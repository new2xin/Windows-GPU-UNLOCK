param(
 [string]$Capture=(Join-Path $PSScriptRoot 'results/20261026_205953_779/GPU-PORT/RUN001'),
 [string]$Clang='C:/Program Files/LLVM/bin/clang.exe',
 [string]$VcVars='C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Auxiliary/Build/vcvars64.bat'
)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$out=Join-Path $PSScriptRoot ('build/release-replay-'+(Get-Date -Format yyyyMMdd_HHmmss_fff))
& python (Join-Path $PSScriptRoot 'make_replay.py') $Capture $out
if($LASTEXITCODE -ne 0){throw 'Strict capture fixture generation failed'}
@('#define GU_WINDOWS_PARTITION_INITIALIZER {0,0,0,{0,0,0,0,0,0,0,0}}','#define GU_INTERNAL_PARTITION_INITIALIZER {0,0,0,{0,0,0,0,0,0,0,0}}') | Set-Content (Join-Path $out 'platform-config.h') -Encoding ascii
$environment=& cmd.exe /d /c "call `"$VcVars`" >nul && set"
if($LASTEXITCODE -ne 0){throw 'MSVC environment failed'}
foreach($line in $environment){if($line -match '^([^=]+)=(.*)$'){[Environment]::SetEnvironmentVariable($matches[1],$matches[2],'Process')}}
$inc=Join-Path $repo 'deps/edk2/MdePkg/Include'
& $Clang -std=c11 -O1 -Wall -Wextra -Werror -Wno-unused-parameter -D_CRT_SECURE_NO_WARNINGS -DGU_PROFILE_MSI_Z790_3080TI -DGU_SINGLE_RTX_CONSOLE -DGU_HEADLESS_RESEARCH -DGU_PORT_PLAN_REVIEW -I $out -I $inc -I ($inc+'/X64') (Join-Path $PSScriptRoot 'test_release.c') (Join-Path $repo 'internal-boot/unlock/checks.c') (Join-Path $repo 'internal-boot/unlock/sha256.c') -o (Join-Path $out 'test-release.exe')
if($LASTEXITCODE -ne 0){throw 'Production release replay compile failed'}
$result=& (Join-Path $out 'test-release.exe') $Capture
if($LASTEXITCODE -ne 0){throw 'Production release replay failed'}
@('#define GU_UNLOCK_SHA256 "0000000000000000000000000000000000000000000000000000000000000000"','#define GU_UNLOCK_BYTES 16') | Set-Content (Join-Path $out 'child-contract.h') -Encoding ascii
& $Clang -std=c11 -O1 -Wall -Wextra -Werror -Wno-unused-parameter -Wno-unused-function -D_CRT_SECURE_NO_WARNINGS -DGU_PROFILE_MSI_Z790_3080TI -DGU_SINGLE_RTX_CONSOLE -DGU_HEADLESS_RESEARCH -DGU_FULL_UNLOCK -DGU_MSI_EXPERIMENTAL_PORT -DGU_AUTOMATIC_BOOT -DGU_USB_MEDIA -DGU_PORT_CHILD_CONTRACT -I $out -I $inc -I ($inc+'/X64') (Join-Path $PSScriptRoot 'test_pre_release_choice.c') (Join-Path $repo 'internal-boot/boot/full-flow.c') (Join-Path $repo 'internal-boot/unlock/sha256.c') -o (Join-Path $out 'test-choice.exe')
if($LASTEXITCODE -ne 0){throw 'Real wrapper choice compile failed'}
$choice=& (Join-Path $out 'test-choice.exe')
if($LASTEXITCODE -ne 0){throw 'Real wrapper choice failed'}
$result=@($result)+@($choice)
$result | Set-Content (Join-Path $out 'test-results.txt') -Encoding ascii
$result
Write-Output ('REPLAY_DIR='+$out)
