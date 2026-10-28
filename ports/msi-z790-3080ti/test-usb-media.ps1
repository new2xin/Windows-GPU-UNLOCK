# GPL-2.0-only. Isolated mocked inventory; no Storage cmdlet or disk mutation.
$ErrorActionPreference='Stop'
$resolver=Join-Path $PSScriptRoot 'Resolve-UsbMedia.ps1'
$mediaFixture=[pscustomobject]@{Checks=0;Disks=$null;Part=$null;Volume=$null}
function Assert-Media([bool]$condition,[string]$message){$mediaFixture.Checks++;if(!$condition){throw $message}}
function Setup-Media {
 $mediaFixture.Disks=@([pscustomobject]@{UniqueId='USBSTOR\DISK&VEN_KIOXIA&PROD_TRANSMEMORY&REV_\PUBLIC-EXAMPLE-SERIAL&0:wk3';Number=4;BusType='USB';FriendlyName='KIOXIA TransMemory';Size=61949214720;IsBoot=$false;IsSystem=$false;PartitionStyle='MBR'})
 $mediaFixture.Part=[pscustomobject]@{Size=61949035008;PartitionNumber=1;DriveLetter='H'}
 $mediaFixture.Volume=[pscustomobject]@{UniqueId='\\?\Volume{00000000-0000-0000-0000-000000000002}\';FileSystem='FAT32';FileSystemLabel='KIOXIA';SizeRemaining=50000000000}
}
function Get-Disk {return $mediaFixture.Disks}
function Get-Partition([int]$DiskNumber,[int]$PartitionNumber){Assert-Media ($DiskNumber -eq $mediaFixture.Disks[0].Number -and $PartitionNumber -eq 1) 'Resolver used a stale disk number';return $mediaFixture.Part}
function Get-Volume {param([Parameter(ValueFromPipeline=$true)]$Partition) process {Assert-Media ($Partition -eq $mediaFixture.Part) 'Resolver used a drive letter instead of its resolved partition';return $mediaFixture.Volume}}
function Assert-Rejected {
 $rejected=$false;try{$null=& $resolver}catch{$rejected=$true}
 Assert-Media $rejected 'Ambiguous or changed media was accepted'
}
Setup-Media;$m=& $resolver;Assert-Media ($m.Root -eq $mediaFixture.Volume.UniqueId -and $m.DriveLetter -eq 'H') 'Baseline media resolution failed'
Setup-Media;$mediaFixture.Disks[0].Number=19;$mediaFixture.Part.DriveLetter='Z';$m=& $resolver;Assert-Media ($m.Disk -eq 19 -and $m.DriveLetter -eq 'Z' -and $m.Root -eq $mediaFixture.Volume.UniqueId) 'Disk number / drive letter change broke resolution'
Setup-Media;$mediaFixture.Part.DriveLetter=$null;$m=& $resolver;Assert-Media (!$m.DriveLetter -and $m.Root -eq $mediaFixture.Volume.UniqueId) 'Letterless volume cannot be accessed'
Setup-Media;$mediaFixture.Volume.FileSystemLabel='MY USB';$mediaFixture.Disks[0].FriendlyName='Renamed device';$m=& $resolver;Assert-Media ($m.Root -eq $mediaFixture.Volume.UniqueId) 'Cosmetic labels became identity requirements'
Setup-Media;$mediaFixture.Disks[0].UniqueId=$mediaFixture.Disks[0].UniqueId.ToLowerInvariant();$m=& $resolver;Assert-Media ($m.Root -eq $mediaFixture.Volume.UniqueId) 'Case-only Windows device path change rejected'
Setup-Media;$mediaFixture.Disks=@();Assert-Rejected
Setup-Media;$mediaFixture.Disks+= $mediaFixture.Disks[0];Assert-Rejected
Setup-Media;$mediaFixture.Disks[0].UniqueId='DIFFERENT USB';Assert-Rejected
Setup-Media;$mediaFixture.Disks[0].IsSystem=$true;Assert-Rejected
Setup-Media;$mediaFixture.Disks[0].Size++;Assert-Rejected
Setup-Media;$mediaFixture.Part.Size--;Assert-Rejected
Setup-Media;$mediaFixture.Volume.UniqueId='\\?\Volume{00000000-0000-0000-0000-000000000000}\';Assert-Rejected
Setup-Media;$mediaFixture.Volume.FileSystem='NTFS';Assert-Rejected
Write-Output ('PASS '+$mediaFixture.Checks+' USB resolver checks: disk/letter/label changes accepted; absent, ambiguous or different media refused; storage fully mocked')
