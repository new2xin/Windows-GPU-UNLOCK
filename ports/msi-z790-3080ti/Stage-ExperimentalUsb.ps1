# GPL-2.0-only. Update the exact existing USB, including USB-first boot mode.
# Never format, delete, change BootOrder/BootNext or initiate a power operation.
param([Parameter(Mandatory)][string]$BuildDirectory,[string]$PreviousBuildDirectory)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$build=[IO.Path]::GetFullPath($BuildDirectory)
$m=Get-Content (Join-Path $build 'manifest.json') -Raw | ConvertFrom-Json
$previous=$null
if($PreviousBuildDirectory){
 $previous=Get-Content (Join-Path $PreviousBuildDirectory 'manifest.json') -Raw | ConvertFrom-Json
 if($previous.Profile -ne $m.Profile -or $previous.Mode -ne $m.Mode){throw 'Previous build identity mismatch'}
}
if($m.Profile -ne 'MSI-PRO-Z790-P-WIFI-DDR4-1.I0-PALIT-RTX3080TI' -or $m.Mode -ne 'experimental-usb-headless-entry-latched-full' -or $m.CanUnlock -ne $true -or $m.HardwareUnlockTested -ne $false){throw 'Wrong experimental build contract'}
foreach($inputFile in $m.Inputs){
 $p=[IO.Path]::GetFullPath((Join-Path $repo $inputFile.Path))
 if(!$p.StartsWith($repo+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase) -or (Get-FileHash -LiteralPath $p).Hash -ne $inputFile.Sha256){throw ('Build input changed: '+$inputFile.Path)}
}
if((Get-FileHash (Join-Path $m.TestsDirectory 'test-results.json')).Hash -ne $m.TestResultSha256){throw 'Host test evidence changed'}
$principal=[Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()
if(!$principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)){throw 'Administrator PowerShell required for storage identity checks'}
function AssertUsb {
 $media=& (Join-Path $PSScriptRoot 'Resolve-UsbMedia.ps1')
 if($media.FreeBytes -lt 200MB){throw 'USB free-space gate failed'}
 return $media
}
$usb=AssertUsb
$board=Get-CimInstance Win32_BaseBoard;$bios=Get-CimInstance Win32_BIOS
if($board.Product -ne 'PRO Z790-P WIFI DDR4 (MS-7E06)' -or $bios.SMBIOSBIOSVersion -ne '1.I0'){throw 'Motherboard/BIOS identity changed'}
if(Confirm-SecureBootUEFI){throw 'Secure Boot enabled'}
$esp=Get-Partition -DiskNumber 0 -PartitionNumber 2
if($esp.Guid.Trim('{}') -ne $m.WindowsEspGuid -or $esp.Size -ne 100MB){throw 'Windows ESP identity changed'}
$stage=Join-Path $PSScriptRoot ('stage/usb-'+(Get-Date -Format yyyyMMdd_HHmmss_fff))
New-Item -ItemType Directory -Path $stage | Out-Null
$before=& (Join-Path $PSScriptRoot 'Read-FirmwareBoot.ps1') -OutputDirectory (Join-Path $stage 'firmware-before') | ConvertFrom-Json
if($null -ne $before.BootNext){throw 'Another one-time boot is scheduled; inspect first'}
$win=@($before.Entries | Where-Object {$_.Active -and $_.Description -ceq 'Windows Boot Manager' -and $_.FilePaths -icontains '\EFI\MICROSOFT\BOOT\BOOTMGFW.EFI'})
$usbBoot=@($before.Entries | Where-Object {$_.Active -and !$_.Hidden -and $_.Description -ceq 'UEFI: KIOXIA, Partition 1'})
$usbFirst=$win.Count -eq 1 -and $usbBoot.Count -eq 1 -and $before.BootOrder[0] -eq $usbBoot[0].Name -and $before.BootOrder[1] -eq $win[0].Name
if($win.Count -ne 1 -or (!$usbFirst -and $before.BootOrder[0] -ne $win[0].Name)){throw 'Unexpected default boot order'}
$existing=@(Get-ChildItem -LiteralPath $usb.Root -File -Force | ForEach-Object {[ordered]@{Name=$_.Name;Bytes=$_.Length;Sha256=(Get-FileHash -LiteralPath $_.FullName).Hash}})
$usb | ConvertTo-Json | Set-Content (Join-Path $stage 'usb-identity.json') -Encoding utf8
$existing | ConvertTo-Json | Set-Content (Join-Path $stage 'retained-root-files.json') -Encoding utf8
$package=Join-Path $build 'package'
$allowed=@('EFI/BOOT/BOOTX64.EFI','EFI/WindowsGPUUnlock/UNLOCK-HEADLESS.EFI','RTX3090-UNLOCK/NVIDIA-LICENSE',
 'RTX3090-UNLOCK/ASSETS/booter_load_ga102.bin','RTX3090-UNLOCK/ASSETS/v67-ga102-610.43.03.bin','RTX3090-UNLOCK/ASSETS/gsp_ga10x.bin',
 'RTX3090-UNLOCK/ASSETS/gsp_boot_ga102.bin','RTX3090-UNLOCK/ASSETS/gsp_boot_desc_ga102.bin')
if($m.Files.Count -ne 8 -or @($m.Files.Path | Select-Object -Unique).Count -ne 8){throw 'Unexpected package file set'}
# Validate the entire copy plan before the first USB write. Updating an EFI
# requires its exact previous build and a verified backup before replacement.
$replacements=@{}
foreach($file in $m.Files){
 if($file.Path -cnotin $allowed){throw 'Unexpected package path'}
 $source=Join-Path $package $file.Path;$destination=Join-Path $usb.Root $file.Path
 if((Get-Item -LiteralPath $source).Length -ne $file.Bytes -or (Get-FileHash -LiteralPath $source).Hash -ne $file.Sha256){throw ('Source mismatch: '+$file.Path)}
 if((Test-Path -LiteralPath $destination) -and (Get-FileHash -LiteralPath $destination).Hash -ne $file.Sha256){
  $prior=@($previous.Files | Where-Object {$_.Path -ceq $file.Path})
  if(!$previous -or $file.Path -cnotin @('EFI/BOOT/BOOTX64.EFI','EFI/WindowsGPUUnlock/UNLOCK-HEADLESS.EFI') -or $prior.Count -ne 1 -or (Get-FileHash -LiteralPath $destination).Hash -ne $prior[0].Sha256){throw ('Unknown existing destination; exact previous EFI required: '+$destination)}
  $replacements[$file.Path]=$prior[0]
 }
 $parent=Split-Path $destination
 while($parent -and $parent -ine $usb.Root){
  if((Test-Path -LiteralPath $parent) -and (Get-Item -LiteralPath $parent -Force).Attributes.HasFlag([IO.FileAttributes]::ReparsePoint)){throw 'Reparse point in USB destination'}
  $parent=Split-Path $parent
 }
}
# Preserve all superseded EFI files on USB and in the workspace before writing
# any replacement. Every existing byte must still match the previous manifest.
$backupStamp=Get-Date -Format yyyyMMdd_HHmmss
$backupRecords=@()
foreach($path in $replacements.Keys){
 $null=AssertUsb;$destination=Join-Path $usb.Root $path;$prior=$replacements[$path]
 if((Get-FileHash -LiteralPath $destination).Hash -ne $prior.Sha256){throw 'Existing EFI changed before backup'}
 $usbBackup=$destination+'.bak-pre-release-review-'+$backupStamp
 $localBackup=Join-Path $stage ('previous-efi/'+$path+'.bak-pre-release-review-'+$backupStamp)
 [void][IO.Directory]::CreateDirectory((Split-Path $localBackup))
 [IO.File]::Copy($destination,$usbBackup,$false);[IO.File]::Copy($destination,$localBackup,$false)
 if((Get-FileHash -LiteralPath $usbBackup).Hash -ne $prior.Sha256 -or (Get-FileHash -LiteralPath $localBackup).Hash -ne $prior.Sha256){throw 'Previous EFI backup mismatch'}
 $backupRecords+=[ordered]@{Path=$path;PreviousSha256=$prior.Sha256;UsbBackup=$usbBackup;LocalBackup=$localBackup}
}
$backupRecords | ConvertTo-Json -Depth 4 | Set-Content (Join-Path $stage 'previous-efi-backups.json') -Encoding utf8
foreach($file in $m.Files){
 $null=AssertUsb
 $source=Join-Path $package $file.Path;$destination=Join-Path $usb.Root $file.Path
 if(!(Test-Path -LiteralPath $destination)){
  [void][IO.Directory]::CreateDirectory((Split-Path $destination))
  [IO.File]::Copy($source,$destination,$false)
 }elseif($replacements.ContainsKey($file.Path)){
  if((Get-FileHash -LiteralPath $destination).Hash -ne $replacements[$file.Path].Sha256){throw 'EFI changed after backup'}
  [IO.File]::Copy($source,$destination,$true)
 }
 if((Get-Item -LiteralPath $destination).Length -ne $file.Bytes -or (Get-FileHash -LiteralPath $destination).Hash -ne $file.Sha256){throw ('Staged mismatch: '+$file.Path)}
}
foreach($file in $existing){if((Get-FileHash -LiteralPath (Join-Path $usb.Root $file.Name)).Hash -ne $file.Sha256){throw 'Original root file changed'}}
$after=& (Join-Path $PSScriptRoot 'Read-FirmwareBoot.ps1') -OutputDirectory (Join-Path $stage 'firmware-after') | ConvertFrom-Json
$rawBefore=Get-Content (Join-Path $stage 'firmware-before/firmware-variables-private.json') -Raw
$rawAfter=Get-Content (Join-Path $stage 'firmware-after/firmware-variables-private.json') -Raw
if($rawBefore -cne $rawAfter){throw 'Firmware variables changed during staging; inspect evidence'}
[ordered]@{Build=$build;BuildManifestSha256=(Get-FileHash (Join-Path $build 'manifest.json')).Hash;Usb=$usb;Files=$m.Files;AllCopiedHashesVerified=$true;OriginalRootFilesVerified=$true;FirmwareVariablesUnchanged=$true;BootNext=$after.BootNext;BootOrder=$after.BootOrder;HardwareUnlockTested=$false;ManualBootRequired=(!$usbFirst);PowerOperationRequested=$false} | ConvertTo-Json -Depth 8 | Set-Content (Join-Path $stage 'stage.json') -Encoding utf8
Write-Output ('USB_STAGE_DIR='+$stage)
