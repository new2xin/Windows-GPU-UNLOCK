param(
 [Parameter(Mandatory)][guid]$WindowsEspGuid,
 [string]$Clang='C:\Program Files\LLVM\bin\clang.exe',
 [string]$LldLink='C:\Program Files\LLVM\bin\lld-link.exe',
 [string]$VcVars='C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat'
)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$out=Join-Path $PSScriptRoot ('build/'+(Get-Date -Format 'yyyyMMdd_HHmmss_fff'))
[void][IO.Directory]::CreateDirectory($out)
$guidBytes=$WindowsEspGuid.ToByteArray()
$data4=($guidBytes[8..15] | ForEach-Object { '0x{0:x2}' -f $_ }) -join ','
$initializer='{0x'+$WindowsEspGuid.ToString('D').Substring(0,8)+',0x'+$WindowsEspGuid.ToString('D').Substring(9,4)+',0x'+$WindowsEspGuid.ToString('D').Substring(14,4)+',{'+$data4+'}}'
@("#define GU_WINDOWS_PARTITION_INITIALIZER $initializer",'#define GU_INTERNAL_PARTITION_INITIALIZER {0,0,0,{0,0,0,0,0,0,0,0}}') | Set-Content (Join-Path $out 'platform-config.h') -Encoding ascii
$inc=Join-Path $repo 'deps/edk2/MdePkg/Include'
$flags=@('-std=c11','-Wall','-Wextra','-Werror','-DGU_PORT_DIAGNOSTIC','-DGU_DIAGNOSTIC_PROBE','-I',$out,'-I',$inc,'-I',($inc+'/X64'))
$sources=@((Join-Path $PSScriptRoot 'main.c'),(Join-Path $PSScriptRoot 'policy.c'),(Join-Path $PSScriptRoot 'probe.c'),(Join-Path $repo 'internal-boot/boot/diagnostic.c'),(Join-Path $repo 'internal-boot/unlock/sha256.c'),(Join-Path $repo 'efi_runtime.c'))
$efiSources=$sources+@((Join-Path $PSScriptRoot 'review.c'),(Join-Path $repo 'internal-boot/boot/full-release.c'),(Join-Path $repo 'internal-boot/unlock/checks.c'))
$reviewFlags=@('-DGU_PROFILE_MSI_Z790_3080TI','-DGU_PORT_PLAN_REVIEW','-DGU_SINGLE_RTX_CONSOLE','-DGU_HEADLESS_RESEARCH')
$objects=@()
foreach($source in $efiSources) {
 $obj=Join-Path $out (([IO.Path]::GetFileNameWithoutExtension($source))+'.obj')
 & $Clang @flags @reviewFlags -target x86_64-pc-windows-msvc -ffreestanding -ffunction-sections -fdata-sections -fno-builtin -fno-stack-protector -mno-red-zone -fshort-wchar -Os -c $source -o $obj
 if($LASTEXITCODE -ne 0){throw "Compile failed: $source"}
 $objects+=$obj
}
$efi=Join-Path $out 'OBSERVE-3080TI.EFI';$map=Join-Path $out 'OBSERVE-3080TI.map'
& $LldLink /nologo /nodefaultlib /subsystem:efi_application /entry:efi_main /machine:x64 /base:0 /dynamicbase /nxcompat /timestamp:0 /opt:ref /opt:icf "/map:$map" "/out:$efi" @objects
if($LASTEXITCODE -ne 0){throw 'EFI link failed'}
$mapText=Get-Content -Raw -LiteralPath $map
foreach($forbidden in @('GuPrepareConsole','GuReleaseGop','GuVerifyReleased','RestoreDecode','unlock_transaction','GuArm')) {
 if($mapText -match ('\b'+$forbidden+'\b')){throw ('Forbidden mutation entry linked: '+$forbidden)}
}
& python (Join-Path $repo 'unlock/verify_pe.py') $efi $map --output (Join-Path $out 'pe-verification.json')
if($LASTEXITCODE -ne 0){throw 'PE verification failed'}
$environment=& cmd.exe /d /c "call `"$VcVars`" >nul && set"
if($LASTEXITCODE -ne 0){throw 'MSVC environment failed'}
foreach($line in $environment){if($line -match '^([^=]+)=(.*)$'){[Environment]::SetEnvironmentVariable($matches[1],$matches[2],'Process')}}
& $Clang @flags -O1 -D_CRT_SECURE_NO_WARNINGS (Join-Path $PSScriptRoot 'test_observer.c') (Join-Path $PSScriptRoot 'policy.c') (Join-Path $PSScriptRoot 'probe.c') (Join-Path $repo 'internal-boot/unlock/sha256.c') -o (Join-Path $out 'test-observer.exe')
if($LASTEXITCODE -ne 0){throw 'Observer test compile failed'}
$test=& (Join-Path $out 'test-observer.exe')
if($LASTEXITCODE -ne 0){throw 'Observer tests failed'}
$test | Set-Content (Join-Path $out 'test-results.txt') -Encoding ascii
& $Clang @flags -O1 -D_CRT_SECURE_NO_WARNINGS (Join-Path $PSScriptRoot 'test_snapshot.c') -o (Join-Path $out 'test-snapshot.exe')
if($LASTEXITCODE -ne 0){throw 'Graph snapshot regression compile failed'}
$snapshotTest=& (Join-Path $out 'test-snapshot.exe')
if($LASTEXITCODE -ne 0){throw 'Graph snapshot regression failed'}
$snapshotTest | Set-Content (Join-Path $out 'snapshot-test-results.txt') -Encoding ascii
$manifest=[ordered]@{
 SchemaVersion=1;Mode='msi-z790-3080ti-observation-only';CanUnlock=$false;CanDetach=$false;WritesPciOrMmio=$false;WritesFirmwareVariables=$false;HardwareTested=$false
 WindowsEspGuid=$WindowsEspGuid.ToString('D');EfiSha256=(Get-FileHash $efi).Hash;EfiBytes=(Get-Item $efi).Length
 SourceCommit=(& git -C $repo rev-parse HEAD);Compiler=(& $Clang --version | Select-Object -First 1)
 Sources=@(($efiSources+@((Join-Path $PSScriptRoot 'observer.h'),(Join-Path $PSScriptRoot 'machine.h'),(Join-Path $PSScriptRoot 'test_observer.c'),(Join-Path $PSScriptRoot 'test_snapshot.c'),(Join-Path $repo 'internal-boot/boot/prepare-console.c'),(Join-Path $repo 'internal-boot/unlock/machine-profile.h'),$PSCommandPath,(Join-Path $out 'platform-config.h'))) | ForEach-Object {[ordered]@{Path=$_;Sha256=(Get-FileHash -LiteralPath $_).Hash}})
 HostTests=$test;SnapshotRegression=$snapshotTest;BuiltUtc=[DateTime]::UtcNow.ToString('o')
}
$manifest | ConvertTo-Json -Depth 5 | Set-Content (Join-Path $out 'manifest.json') -Encoding UTF8
Write-Output $test
Write-Output $snapshotTest
Write-Output "Built diagnostic only: $out"
