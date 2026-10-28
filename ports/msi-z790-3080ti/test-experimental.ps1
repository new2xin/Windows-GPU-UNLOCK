# GPL-2.0-only. Actual production app/core with all EFI/GPU effects simulated.
param(
 [string]$Capture=(Join-Path $PSScriptRoot 'results/20261026_215558_245/GPU-PORT/RUN002'),
 [string]$Assets=(Join-Path $PSScriptRoot 'build/firmware-20261026_210934_481/assets-py312'),
 [string]$Clang='C:/Program Files/LLVM/bin/clang.exe',
 [string]$VcVars='C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Auxiliary/Build/vcvars64.bat'
)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$unlock=Join-Path $repo 'internal-boot/unlock'
$out=Join-Path $PSScriptRoot ('build/transaction-'+(Get-Date -Format yyyyMMdd_HHmmss_fff))
& python (Join-Path $PSScriptRoot 'make_stock_fixture.py') $Capture $out
if($LASTEXITCODE -ne 0){throw 'v4 fixture verification failed'}
& python (Join-Path $PSScriptRoot 'make_child_replay.py') (Join-Path $PSScriptRoot 'results/unlock-20261027_101738_832/HLS000003.TXT') $out
if($LASTEXITCODE -ne 0){throw 'Physical v5 child failure fixture verification failed'}
foreach($asset in (Get-Content (Join-Path $unlock 'assets.json') -Raw | ConvertFrom-Json)){
 $p=Join-Path $Assets $asset.Name
 if((Get-Item $p).Length -ne $asset.Bytes -or (Get-FileHash $p).Hash -ne $asset.Sha256){throw ('Asset mismatch: '+$asset.Name)}
}
@('#define GU_WINDOWS_PARTITION_INITIALIZER {0,0,0,{0,0,0,0,0,0,0,1}}',
 '#define GU_INTERNAL_PARTITION_INITIALIZER {0,0,0,{0,0,0,0,0,0,0,0}}') | Set-Content (Join-Path $out 'platform-config.h') -Encoding ascii
$environment=& cmd.exe /d /c "call `"$VcVars`" >nul && set"
if($LASTEXITCODE -ne 0){throw 'MSVC environment failed'}
foreach($line in $environment){if($line -match '^([^=]+)=(.*)$'){[Environment]::SetEnvironmentVariable($matches[1],$matches[2],'Process')}}
$inc=Join-Path $repo 'deps/edk2/MdePkg/Include'
$flags=@('-std=c11','-O1','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-unused-function','-D_CRT_SECURE_NO_WARNINGS',
 '-D__KERNEL__','-DGU_UEFI_PORT','-DGU_TARGET_PCI_ID=0x220810deU','-DGU_EXPERIMENTAL_3090','-DGU_ENTRY_LATCHED_SS1_FULL',
 '-DGU_INTERNAL_BOOT','-DGU_AUTOMATIC_BOOT','-DGU_HEADLESS_RESEARCH','-DGU_USB_MEDIA','-DGU_PROFILE_MSI_Z790_3080TI','-DGU_MSI_EXPERIMENTAL_PORT',
 '-I',$out,'-I',(Join-Path $unlock 'compat'),'-I',$inc,'-I',($inc+'/X64'))
$core=@('port.c','compat.c','vendor/unlock_610.c','vendor/prep_610.c','vendor/layout_610.c','vendor/vbios_610.c','vendor/gfw_eval.c') | ForEach-Object {Join-Path $unlock $_}
$prom=Join-Path $Capture 'PROM.BIN';$stock=Join-Path $out 'stock.txt'
$jobs=@(@{Name='core';Sources=@((Join-Path $unlock 'tests/test_efi_transaction.c'))+$core;Args=@($prom,$Assets,$stock)},
 @{Name='headless';Sources=@('tests/test_headless.c','app_io.c','ownership.c','format.c','sha256.c','checks.c') | ForEach-Object {Join-Path $unlock $_};Args=@($prom,$stock,$Assets)})
$results=@()
foreach($job in $jobs){
 $exe=Join-Path $out ($job.Name+'.exe');$sources=$job.Sources
 if($job.Name -eq 'headless'){$sources+=$core}
 & $Clang @flags @sources -o $exe
 if($LASTEXITCODE -ne 0){throw ('Compile failed: '+$job.Name)}
 $lines=& $exe @($job.Args)
 $lines | Set-Content (Join-Path $out ($job.Name+'.log')) -Encoding ascii
 if($LASTEXITCODE -ne 0){throw ('Host simulation failed: '+$job.Name)}
 $results+=[ordered]@{Name=$job.Name;Passed=$true;Summary=$lines[-1];LogSha256=(Get-FileHash (Join-Path $out ($job.Name+'.log'))).Hash}
 $lines | Select-Object -Last 3
}
[ordered]@{AllPassed=$true;HardwareAccess=$false;HardwareUnlockTested=$false;Fixture=(Get-Content (Join-Path $out 'stock-provenance.json') -Raw | ConvertFrom-Json);Results=$results} | ConvertTo-Json -Depth 8 | Set-Content (Join-Path $out 'test-results.json') -Encoding utf8
Write-Output ('TRANSACTION_DIR='+$out)
