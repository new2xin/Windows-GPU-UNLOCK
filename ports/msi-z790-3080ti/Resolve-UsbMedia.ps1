# GPL-2.0-only. Read-only identity resolver; no fixed drive letter/disk number.
param()
$ErrorActionPreference='Stop'
$mediaCandidates=@(Get-Disk | Where-Object {$_.UniqueId -ieq 'USBSTOR\DISK&VEN_KIOXIA&PROD_TRANSMEMORY&REV_\PUBLIC-EXAMPLE-SERIAL&0:wk3'})
if($mediaCandidates.Count -ne 1){throw 'Expected exactly one KIOXIA TransMemory device'}
$disk=$mediaCandidates[0]
if($disk.BusType -ne 'USB' -or $disk.Size -ne 61949214720 -or $disk.IsBoot -or $disk.IsSystem -or $disk.PartitionStyle -ne 'MBR'){throw 'KIOXIA device identity/layout mismatch'}
$parts=@(Get-Partition -DiskNumber $disk.Number -PartitionNumber 1)
if($parts.Count -ne 1 -or $parts[0].Size -ne 61949035008){throw 'KIOXIA partition identity/layout mismatch'}
$volumes=@($parts[0] | Get-Volume)
if($volumes.Count -ne 1){throw 'Expected exactly one KIOXIA volume'}
$volume=$volumes[0]
if($volume.UniqueId -ine '\\?\Volume{00000000-0000-0000-0000-000000000002}\' -or $volume.FileSystem -ne 'FAT32'){throw 'KIOXIA volume identity mismatch'}
# Access through the stable volume path, even if its letter changes mid-run.
[pscustomobject][ordered]@{Disk=$disk.Number;FriendlyName=$disk.FriendlyName;UniqueId=$disk.UniqueId;Bytes=$disk.Size;Partition=$parts[0].PartitionNumber;Volume=$volume.UniqueId;Root=$volume.UniqueId;DriveLetter=[string]$parts[0].DriveLetter;FileSystem=$volume.FileSystem;FreeBytes=$volume.SizeRemaining}
