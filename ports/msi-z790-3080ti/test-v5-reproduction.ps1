# GPL-2.0-only. Rebuild v5 production child sources in an isolated host overlay.
# Uses fake EFI/GPU services only. Does not create or install an EFI image.
param([Parameter(Mandatory)][string]$TestDirectory,
 [string]$Clang='C:/Program Files/LLVM/bin/clang.exe',
 [string]$VcVars='C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Auxiliary/Build/vcvars64.bat')
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$out=Join-Path $PSScriptRoot ('build/v5-reproduction-'+(Get-Date -Format yyyyMMdd_HHmmss_fff))
$overlay=Join-Path $out 'source';[void][IO.Directory]::CreateDirectory($overlay)
$original=Join-Path $repo 'internal-boot/unlock'
$target=Join-Path $overlay 'internal-boot/unlock'
foreach($file in (Get-ChildItem $original -Recurse -File | Where-Object {$_.Extension -in '.h','.c' -and $_.FullName.Substring($original.Length+1) -notmatch '(^|[\\/])build([\\/]|$)'})) {
 $destination=Join-Path $target $file.FullName.Substring($original.Length+1)
 [void][IO.Directory]::CreateDirectory((Split-Path $destination));[IO.File]::Copy($file.FullName,$destination,$false)
}
# Retain the current overlay versions before substituting exact v5 originals.
foreach($name in @('app.c','headless-policy.h')) {
 $destination=Join-Path $target $name
 $manifest=Get-Content (Join-Path $PSScriptRoot 'build/experimental-20261027_020515_738/manifest.json') -Raw | ConvertFrom-Json
 $expected=@($manifest.Inputs | Where-Object {$_.Path -ceq ('internal-boot/unlock/'+$name)})
 if($expected.Count -ne 1 -or (Get-FileHash (Join-Path $original ($name+'.bak-child-arming-v6-20261027_102023'))).Hash -ine $expected[0].Sha256){throw 'v5 production source provenance mismatch'}
 [IO.File]::Copy($destination,($destination+'.bak-v6-overlay-'+(Get-Date -Format yyyyMMdd_HHmmss)),$false)
 [IO.File]::Copy((Join-Path $original ($name+'.bak-child-arming-v6-20261027_102023')),$destination,$true)
}
$copies=@{'internal-boot/boot/media.h'='internal-boot/boot/media.h';'ports/msi-z790-3080ti/test_child_graph.h'='ports/msi-z790-3080ti/test_child_graph.h';'ports/msi-z790-3080ti/machine.h'='ports/msi-z790-3080ti/machine.h'}
foreach($entry in $copies.GetEnumerator()) {
 $destination=Join-Path $overlay $entry.Value;[void][IO.Directory]::CreateDirectory((Split-Path $destination))
 [IO.File]::Copy((Join-Path $repo $entry.Key),$destination,$false)
}
$environment=& cmd.exe /d /c "call `"$VcVars`" >nul && set"
if($LASTEXITCODE -ne 0){throw 'MSVC environment failed'}
foreach($line in $environment){if($line -match '^([^=]+)=(.*)$'){[Environment]::SetEnvironmentVariable($matches[1],$matches[2],'Process')}}
$inc=Join-Path $repo 'deps/edk2/MdePkg/Include'
$flags=@('-std=c11','-O1','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-unused-function','-D_CRT_SECURE_NO_WARNINGS',
 '-D__KERNEL__','-DGU_UEFI_PORT','-DGU_TARGET_PCI_ID=0x220810deU','-DGU_EXPERIMENTAL_3090','-DGU_ENTRY_LATCHED_SS1_FULL',
 '-DGU_INTERNAL_BOOT','-DGU_AUTOMATIC_BOOT','-DGU_HEADLESS_RESEARCH','-DGU_USB_MEDIA','-DGU_PROFILE_MSI_Z790_3080TI','-DGU_MSI_EXPERIMENTAL_PORT','-DGU_TEST_LEGACY_CHILD',
 '-I',$TestDirectory,'-I',(Join-Path $target 'compat'),'-I',$inc,'-I',($inc+'/X64'))
$sources=@('tests/test_headless.c','app_io.c','ownership.c','format.c','sha256.c','checks.c','port.c','compat.c','vendor/unlock_610.c','vendor/prep_610.c','vendor/layout_610.c','vendor/vbios_610.c','vendor/gfw_eval.c') | ForEach-Object {Join-Path $target $_}
$exe=Join-Path $out 'v5-host-only.exe'
& $Clang @flags @sources -o $exe
if($LASTEXITCODE -ne 0){throw 'v5 reproduction compile failed'}
$capture=Join-Path $PSScriptRoot 'results/20261026_215558_245/GPU-PORT/RUN002/PROM.BIN'
$assets=Join-Path $PSScriptRoot 'build/firmware-20261026_210934_481/assets-py312'
$lines=& $exe $capture (Join-Path $TestDirectory 'stock.txt') $assets
$lines | Set-Content (Join-Path $out 'test-results.txt') -Encoding ascii
if($LASTEXITCODE -ne 0){throw 'v5 reproduction failed'}
$lines | Select-Object -Last 18
Write-Output ('V5_REPRODUCTION_DIR='+$out)
