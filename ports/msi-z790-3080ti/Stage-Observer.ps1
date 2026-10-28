param([Parameter(Mandatory)][string]$BuildDirectory,[switch]$Register,[string]$Description='RTX 3080 Ti - READ ONLY DIAGNOSTIC')
$ErrorActionPreference='Stop'
$build=[IO.Path]::GetFullPath($BuildDirectory)
$manifest=Get-Content -LiteralPath (Join-Path $build 'manifest.json') -Raw | ConvertFrom-Json
if($manifest.Mode -ne 'msi-z790-3080ti-observation-only' -or $manifest.CanUnlock -ne $false -or $manifest.CanDetach -ne $false -or $manifest.WritesPciOrMmio -ne $false -or $manifest.WritesFirmwareVariables -ne $false){throw 'Wrong build capabilities'}
$efi=Join-Path $build 'OBSERVE-3080TI.EFI'
if((Get-FileHash -LiteralPath $efi).Hash -ne $manifest.EfiSha256){throw 'EFI hash changed'}
$identity=[Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()
if(!$identity.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)){throw 'Run from an administrator PowerShell'}
$partition=Get-Partition -DiskNumber 0 -PartitionNumber 2
if($partition.Guid.Trim('{}') -ne $manifest.WindowsEspGuid -or $partition.GptType -ne '{c12a7328-f81f-11d2-ba4b-00a0c93ec93b}' -or $partition.Size -ne 100MB){throw 'System ESP identity changed'}
if(Confirm-SecureBootUEFI){throw 'Secure Boot currently enabled'}
$stage=Join-Path $PSScriptRoot ('stage/'+(Get-Date -Format 'yyyyMMdd_HHmmss_fff'))
[void][IO.Directory]::CreateDirectory($stage)
& bcdedit /export (Join-Path $stage 'BCD.bak-observer')
if($LASTEXITCODE -ne 0){throw 'BCD backup failed'}
$before=@(& bcdedit /enum firmware)
if($LASTEXITCODE -ne 0){throw 'Firmware inventory failed'}
$before | Set-Content (Join-Path $stage 'firmware-before.txt') -Encoding UTF8
# Select an unused access letter; never replace an existing mount.
$letter=@('U','V','W','X','Y','Z') | Where-Object {!(Test-Path ($_+':\')) -and !(Get-PSDrive $_ -ErrorAction SilentlyContinue)} | Select-Object -First 1
if(!$letter){throw 'No unused mount letter'}
$mount=$letter+':';$mounted=$false
try {
 & mountvol $mount /S
 if($LASTEXITCODE -ne 0){throw 'ESP mount failed'}
 $mounted=$true
 # mountvol /S mounts ESPs without assigning a Storage-CIM DriveLetter.
 $mountedVolume=(@(& mountvol $mount /L) -join '').Trim()
 if($LASTEXITCODE -ne 0 -or $partition.AccessPaths.Count -ne 1 -or $mountedVolume -ne $partition.AccessPaths[0]){throw 'Mounted ESP is not the expected volume'}
 $volume=New-Object IO.DriveInfo ($mount+'\')
 if($volume.DriveFormat -ne 'FAT32' -or $volume.AvailableFreeSpace -lt 49MB){throw 'Insufficient diagnostic free space'}
 $backup=Join-Path $stage ('ESP.bak-observer-'+(Get-Date -Format 'yyyyMMdd_HHmmss'))
 # The live BCD and transaction logs are exported through bcdedit above;
 # copying those locked files directly is not a consistent BCD backup.
 & robocopy ($mount+'\') $backup /E /COPY:DAT /DCOPY:DA /R:0 /W:0 /XJ /XF BCD 'BCD.LOG*' /NFL /NDL /NJH /NJS > (Join-Path $stage 'esp-backup.log')
 if($LASTEXITCODE -ge 8){throw 'ESP backup failed'}
 $backupFiles=@(Get-ChildItem -LiteralPath $backup -File -Recurse -Force)
 if(!($backupFiles | Where-Object {$_.FullName -like '*\Microsoft\Boot\bootmgfw.efi'})){throw 'Windows loader missing from backup'}
 $backupFiles | ForEach-Object {
   $relative=$_.FullName.Substring($backup.Length+1)
   $source=Join-Path ($mount+'\') $relative
   $hash=(Get-FileHash -LiteralPath $_.FullName).Hash
   if((Get-FileHash -LiteralPath $source).Hash -ne $hash){throw "Backup mismatch: $relative"}
   [ordered]@{Path=$relative;Bytes=$_.Length;SHA256=$hash}
 } | ConvertTo-Json -Depth 3 | Set-Content (Join-Path $stage 'esp-backup-manifest.json') -Encoding UTF8
 $folder='GPUObserver3080Ti-'+$manifest.EfiSha256.Substring(0,12)
 $relative='\EFI\'+$folder+'\OBSERVE.EFI'
 $destination=Join-Path ($mount+'\') $relative.TrimStart('\')
 if(Test-Path -LiteralPath (Split-Path $destination)){throw 'Observer destination exists; no overwrite performed'}
 [void][IO.Directory]::CreateDirectory((Split-Path $destination))
 [IO.File]::Copy($efi,$destination,$false)
 if((Get-FileHash -LiteralPath $destination).Hash -ne $manifest.EfiSha256){throw 'Staged EFI hash mismatch'}
 $entry=$null
 if($Register) {
   $copy=@(& bcdedit /copy '{bootmgr}' /d $Description)
   if($LASTEXITCODE -ne 0){throw 'Manual boot entry creation failed'}
   $ids=@([regex]::Matches(($copy -join "`n"),'\{[0-9a-fA-F-]{36}\}') | ForEach-Object {$_.Value})
   if($ids.Count -ne 1){throw 'Cannot identify new boot entry; inspect BCD manually'}
   $entry=$ids[0]
   # Persist the newly allocated identity before any further configuration.
   $entry | Set-Content (Join-Path $stage 'created-entry.txt') -Encoding ascii
   foreach($arguments in @(@('/set',$entry,'device',('partition='+$mount)),@('/set',$entry,'path',$relative),@('/set','{fwbootmgr}','displayorder',$entry,'/addlast'))) {
     & bcdedit @arguments
     if($LASTEXITCODE -ne 0){throw "Boot entry configuration failed; remove only $entry using the saved identity"}
   }
 }
 $after=@(& bcdedit /enum firmware)
 if($LASTEXITCODE -ne 0){throw 'Post-stage firmware inventory failed'}
 $after | Set-Content (Join-Path $stage 'firmware-after.txt') -Encoding UTF8
 [ordered]@{Mode=$manifest.Mode;EfiSha256=$manifest.EfiSha256;EspGuid=$partition.Guid;EfiPath=$relative;Entry=$entry;DefaultBootChanged=$false;RebootRequested=$false;Build=$build;Backup=$backup} | ConvertTo-Json | Set-Content (Join-Path $stage 'stage.json') -Encoding UTF8
 Write-Output "Staged diagnostic. Manual entry: $entry. State: $stage. No restart requested."
} finally {
 if($mounted){& mountvol $mount /D;if($LASTEXITCODE -ne 0){Write-Warning "Temporary ESP mount remains at $mount"}}
}
