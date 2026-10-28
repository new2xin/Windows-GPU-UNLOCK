# GPL-2.0-only. MSI 3080 Ti experimental USB package, isolated outputs only.
param(
 [Parameter(Mandatory)][guid]$WindowsEspGuid,
 [string]$Capture=(Join-Path $PSScriptRoot 'results/20261026_215558_245/GPU-PORT/RUN002'),
 [string]$Assets=(Join-Path $PSScriptRoot 'build/firmware-20261026_210934_481/assets-py312'),
 [string]$Clang='C:/Program Files/LLVM/bin/clang.exe',
 [string]$LldLink='C:/Program Files/LLVM/bin/lld-link.exe'
)
$ErrorActionPreference='Stop'
if($WindowsEspGuid -eq [guid]::Empty -or $WindowsEspGuid -eq [guid]'00000000-0000-0000-0000-000000000001'){throw 'Provide the reviewed Windows ESP GUID for your own system'}
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$unlock=Join-Path $repo 'internal-boot/unlock';$boot=Join-Path $repo 'internal-boot/boot'
if(Test-Path (Join-Path $boot 'platform-config.h')){throw 'Source-level platform-config.h would shadow isolated configuration; inspect first'}
$out=Join-Path $PSScriptRoot ('build/experimental-'+(Get-Date -Format yyyyMMdd_HHmmss_fff))
& python (Join-Path $PSScriptRoot 'make_stock_fixture.py') $Capture $out
if($LASTEXITCODE -ne 0){throw 'v4 capture verification failed'}
function Inventory {
 $paths=@()
 foreach($root in @((Join-Path $repo 'internal-boot'),$PSScriptRoot,(Join-Path $repo 'deps/edk2/MdePkg/Include'))){
  $paths+=Get-ChildItem -LiteralPath $root -Recurse -File | Where-Object {$_.Extension -in '.c','.h','.ps1','.py','.json' -and $_.FullName.Substring($root.Length+1) -notmatch '(^|[\\/])(build|results|stage)([\\/]|$)'}
 }
 @($paths | Sort-Object FullName -Unique | ForEach-Object {[ordered]@{Path=$_.FullName.Substring($repo.Length+1).Replace([char]92,[char]47);Sha256=(Get-FileHash -LiteralPath $_.FullName).Hash.ToLowerInvariant()}})
}
$before=Inventory
# Tests are part of this build gate, against the same unchanged source inventory.
$mediaTest=& (Join-Path $PSScriptRoot 'test-usb-media.ps1')
$mediaTest | Set-Content (Join-Path $out 'media-resolver-tests.txt') -Encoding ascii
$mediaTest | ForEach-Object {Write-Output $_}
$policyOutput=& (Join-Path $PSScriptRoot 'test-port.ps1') -Capture $Capture -Clang $Clang
$policyOutput | ForEach-Object {Write-Output $_}
$policyDirs=@($policyOutput | Where-Object {$_ -like 'REPLAY_DIR=*'})
if($policyDirs.Count -ne 1){throw 'Missing successful release/real-wrapper policy tests'}
$policyTests=$policyDirs[0].Substring('REPLAY_DIR='.Length)
$testOutput=& (Join-Path $PSScriptRoot 'test-experimental.ps1') -Capture $Capture -Assets $Assets -Clang $Clang
$testOutput | ForEach-Object {Write-Output $_}
$testDir=@($testOutput | Where-Object {$_ -like 'TRANSACTION_DIR=*'})
if($testDir.Count -ne 1){throw 'Missing successful transaction test result'}
$tests=$testDir[0].Substring('TRANSACTION_DIR='.Length)
$tm=Get-Content (Join-Path $tests 'test-results.json') -Raw | ConvertFrom-Json
if(!$tm.AllPassed -or $tm.HardwareAccess){throw 'Host test gate failed'}
$guidBytes=$WindowsEspGuid.ToByteArray()
$windowsInitializer=('{{0x{0:x8},0x{1:x4},0x{2:x4},{{{3}}}}}' -f [BitConverter]::ToUInt32($guidBytes,0),[BitConverter]::ToUInt16($guidBytes,4),[BitConverter]::ToUInt16($guidBytes,6),(($guidBytes[8..15] | ForEach-Object {'0x{0:x2}' -f $_}) -join ','))
@("#define GU_WINDOWS_PARTITION_INITIALIZER $windowsInitializer",
 '#define GU_INTERNAL_PARTITION_INITIALIZER {0,0,0,{0,0,0,0,0,0,0,0}}') | Set-Content (Join-Path $out 'platform-config.h') -Encoding ascii
$inc=Join-Path $repo 'deps/edk2/MdePkg/Include'
$common=@('-target','x86_64-pc-windows-msvc','-std=c11','-ffreestanding','-fno-builtin','-fno-stack-protector','-mno-red-zone','-fshort-wchar','-Os','-Wall','-Wextra','-Werror','-Wno-unused-parameter',
 '-DGU_PROFILE_MSI_Z790_3080TI','-DGU_MSI_EXPERIMENTAL_PORT','-DGU_HEADLESS_RESEARCH','-DGU_AUTOMATIC_BOOT','-DGU_USB_MEDIA','-I',$out,'-I',$inc,'-I',($inc+'/X64'))
$childSources=@('app','app_io','ownership','format','runtime','sha256','checks','port','compat','vendor/unlock_610','vendor/prep_610','vendor/layout_610','vendor/vbios_610','vendor/gfw_eval') | ForEach-Object {Join-Path $unlock ($_.ToString()+'.c')}
$wrapperSources=@('handoff.c','full-release.c','full-flow.c','diagnostic.c','../unlock/checks.c','../unlock/sha256.c','../efi_runtime.c') | ForEach-Object {Join-Path $boot $_}
function BuildImage([string]$Name,[string[]]$Sources,[string[]]$Defines){
 $dir=Join-Path $out $Name;New-Item -ItemType Directory -Path $dir | Out-Null
 $objects=@()
 foreach($source in $Sources){
  $obj=Join-Path $dir ([IO.Path]::GetFileNameWithoutExtension($source)+'.obj')
  & $Clang @common @Defines -c $source -o $obj
  if($LASTEXITCODE -ne 0){throw ('EFI compile failed: '+$source)}
  $objects+=$obj
 }
 & $LldLink /nologo /nodefaultlib /subsystem:efi_application /entry:efi_main /machine:x64 /base:0 /dynamicbase /nxcompat /timestamp:0 /opt:ref /opt:icf "/map:$dir/$Name.map" "/out:$dir/$Name.EFI" @objects
 if($LASTEXITCODE -ne 0){throw ('EFI link failed: '+$Name)}
 & python (Join-Path $unlock 'verify_pe.py') "$dir/$Name.EFI" "$dir/$Name.map" --output "$dir/pe-verification.json"
 if($LASTEXITCODE -ne 0){throw ('PE validation failed: '+$Name)}
}
BuildImage 'CHILD' $childSources @('-D__KERNEL__','-DGU_UEFI_PORT','-DGU_TARGET_PCI_ID=0x220810deU','-DGU_EXPERIMENTAL_3090','-DGU_ENTRY_LATCHED_SS1_FULL','-DGU_INTERNAL_BOOT','-I',(Join-Path $unlock 'compat'))
$child=Join-Path $out 'CHILD/CHILD.EFI';$childHash=(Get-FileHash $child).Hash.ToLowerInvariant()
@('/* Private child bytes and digest contract. */',('#define GU_UNLOCK_SHA256 "'+$childHash+'"'),('#define GU_UNLOCK_BYTES '+(Get-Item $child).Length)) | Set-Content (Join-Path $out 'child-contract.h') -Encoding ascii
BuildImage 'WRAPPER' $wrapperSources @('-DGU_SINGLE_RTX_CONSOLE','-DGU_FULL_UNLOCK','-DGU_PORT_CHILD_CONTRACT')
$package=Join-Path $out 'package'
foreach($dir in @('EFI/BOOT','EFI/WindowsGPUUnlock','RTX3090-UNLOCK/ASSETS')){New-Item -ItemType Directory -Path (Join-Path $package $dir) | Out-Null}
Copy-Item (Join-Path $out 'WRAPPER/WRAPPER.EFI') (Join-Path $package 'EFI/BOOT/BOOTX64.EFI')
Copy-Item $child (Join-Path $package 'EFI/WindowsGPUUnlock/UNLOCK-HEADLESS.EFI')
foreach($asset in (Get-Content (Join-Path $unlock 'assets.json') -Raw | ConvertFrom-Json)){
 $p=Join-Path $Assets $asset.Name
 if((Get-Item $p).Length -ne $asset.Bytes -or (Get-FileHash $p).Hash -ne $asset.Sha256){throw ('Asset changed: '+$asset.Name)}
 Copy-Item $p (Join-Path $package ('RTX3090-UNLOCK/ASSETS/'+$asset.Name))
}
Copy-Item (Join-Path $Assets 'NVIDIA-LICENSE') (Join-Path $package 'RTX3090-UNLOCK/NVIDIA-LICENSE')
$after=Inventory
if(($before | ConvertTo-Json -Depth 4 -Compress) -cne ($after | ConvertTo-Json -Depth 4 -Compress)){throw 'Inputs changed during tests/build; package cannot be released'}
$files=@(Get-ChildItem $package -Recurse -File | Sort-Object FullName | ForEach-Object {[ordered]@{Path=$_.FullName.Substring($package.Length+1).Replace([char]92,[char]47);Bytes=$_.Length;Sha256=(Get-FileHash $_.FullName).Hash.ToLowerInvariant()}})
[ordered]@{Schema=1;Profile='MSI-PRO-Z790-P-WIFI-DDR4-1.I0-PALIT-RTX3080TI';Mode='experimental-usb-headless-entry-latched-full';WrapperVersion=6;ChildVersion=6;ChildDependencyScopedGraph=$true;ChildInputReset=$false;ChildSnapshotReadAttempts=3;ChildPhysicalGraphReplay=$true;ChildFailureDiagnostics=$true;CanUnlock=$true;HardwareUnlockTested=$false;BuiltUtc=[DateTime]::UtcNow.ToString('o');WindowsEspGuid=$WindowsEspGuid.ToString();ChildSha256=$childHash;CompletePolicyReviewBeforeDetach=$true;DetailedReleaseRecheckLabels=$true;DependencyScopedGraph=$true;GraphPolicy='runtime-GPU-children-owners-console-input-boundary-union-both-snapshots';FixedUsbTopologyRequired=$false;SnapshotReadAttempts=3;UsbEnumerationWaitBounded=$false;SkipReviewGraphCaptured=$true;ColdDataDisplayedAsStatus=$false;PolicyTestsDirectory=$policyTests;PolicyTestResultSha256=(Get-FileHash (Join-Path $policyTests 'test-results.txt')).Hash;TestsDirectory=$tests;TestResultSha256=(Get-FileHash (Join-Path $tests 'test-results.json')).Hash;Capture=(Get-Content (Join-Path $out 'stock-provenance.json') -Raw | ConvertFrom-Json);Files=$files;Inputs=$before} | ConvertTo-Json -Depth 10 | Set-Content (Join-Path $out 'manifest.json') -Encoding utf8
Write-Output ('EXPERIMENTAL_DIR='+$out)
