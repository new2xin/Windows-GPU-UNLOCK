param([string]$Clang='C:/Program Files/LLVM/bin/clang.exe',
 [string]$VcVars='C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Auxiliary/Build/vcvars64.bat')
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$boot=Join-Path $repo 'internal-boot/boot'
$out=Join-Path $PSScriptRoot ('build/regression-'+(Get-Date -Format yyyyMMdd_HHmmss_fff))
New-Item -ItemType Directory -Path $out | Out-Null
@('#define GU_WINDOWS_PARTITION_INITIALIZER {0,0,0,{0,0,0,0,0,0,0,0}}','#define GU_INTERNAL_PARTITION_INITIALIZER {0,0,0,{0,0,0,0,0,0,0,0}}') | Set-Content (Join-Path $out 'platform-config.h') -Encoding ascii
$environment=& cmd.exe /d /c "call `"$VcVars`" >nul && set"
if($LASTEXITCODE -ne 0){throw 'MSVC environment failed'}
foreach($line in $environment){if($line -match '^([^=]+)=(.*)$'){[Environment]::SetEnvironmentVariable($matches[1],$matches[2],'Process')}}
$inc=Join-Path $repo 'deps/edk2/MdePkg/Include'
$flags=@('-std=c11','-O1','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-D_CRT_SECURE_NO_WARNINGS','-I',$out,'-I',$inc,'-I',($inc+'/X64'))
$results=@()
foreach($profile in @('GU_PROFILE_B650M_3090FE','GU_PROFILE_B660M_3070TI','GU_PROFILE_X870_3090TI')) {
 $exe=Join-Path $out ($profile+'.exe')
 & $Clang @flags "-D$profile" (Join-Path $boot 'test_cold_precheck.c') (Join-Path $repo 'internal-boot/unlock/checks.c') (Join-Path $repo 'internal-boot/unlock/sha256.c') -o $exe
 if($LASTEXITCODE -ne 0){throw ('Cold regression compile failed: '+$profile)}
 $result=& $exe
 if($LASTEXITCODE -ne 0){throw ('Cold regression failed: '+$profile)}
 $results+=($profile+': '+($result -join ' '))
}
& $Clang @flags (Join-Path $boot 'test_full_flow.c') (Join-Path $boot 'full-flow.c') -o (Join-Path $out 'test-full-flow.exe')
if($LASTEXITCODE -ne 0){throw 'Flow regression compile failed'}
$results+=(& (Join-Path $out 'test-full-flow.exe'))
if($LASTEXITCODE -ne 0){throw 'Flow regression failed'}
# The new profile cannot be accidentally compiled into a live write image.
'#include "machine-profile.h"' | Set-Content (Join-Path $out 'write-guard.c') -Encoding ascii
& $Clang @flags -DGU_PROFILE_MSI_Z790_3080TI -DGU_EXPERIMENTAL_3090 -I (Join-Path $repo 'internal-boot/unlock') -c (Join-Path $out 'write-guard.c') -o (Join-Path $out 'forbidden.obj') 2> (Join-Path $out 'write-guard.log')
if($LASTEXITCODE -eq 0 -or (Get-Content -Raw (Join-Path $out 'write-guard.log')) -notlike '*write builds require explicit*'){throw 'Write-build rejection gate failed'}
$results+='PASS MSI accidental write build rejected; explicit USB headless experiment required'
$results | Set-Content (Join-Path $out 'test-results.txt') -Encoding ascii
$results
Write-Output ('REGRESSION_DIR='+$out)
