# GPL-2.0-only. Read USB logs into a fresh directory; no hardware mutation.
param()
$ErrorActionPreference='Stop'
$media=& (Join-Path $PSScriptRoot 'Resolve-UsbMedia.ps1')
$source=Join-Path $media.Root 'RTX3090-UNLOCK'
$out=Join-Path $PSScriptRoot ('results/unlock-'+(Get-Date -Format yyyyMMdd_HHmmss_fff))
New-Item -ItemType Directory -Path $out | Out-Null
$files=@(Get-ChildItem -LiteralPath $source -Recurse -File | Where-Object {$_.Name -match '^HLS[0-9]{6}\.TXT$' -or ($_.Name -eq 'REPORT.TXT' -and $_.Directory.Name -match '^AUTO[0-9]{6}$')})
$manifest=@()
foreach($file in $files){
 $relative=$file.FullName.Substring($source.Length+1)
 if($file.Attributes.HasFlag([IO.FileAttributes]::ReparsePoint)){throw 'Log reparse point refused'}
 $before=(Get-FileHash -LiteralPath $file.FullName).Hash
 $destination=Join-Path $out $relative
 [void][IO.Directory]::CreateDirectory((Split-Path $destination))
 [IO.File]::Copy($file.FullName,$destination,$false)
 if((Get-FileHash -LiteralPath $destination).Hash -ne $before -or (Get-FileHash -LiteralPath $file.FullName).Hash -ne $before){throw 'Log changed during collection'}
 $manifest+=[ordered]@{Path=$relative;Bytes=$file.Length;Sha256=$before;LastWriteTime=$file.LastWriteTime.ToString('o')}
}
$manifest | ConvertTo-Json -Depth 4 | Set-Content (Join-Path $out 'log-manifest.json') -Encoding utf8
$gpu=@(& nvidia-smi --query-gpu=name,memory.total,pci.device_id,pci.sub_device_id,driver_version --format=csv,noheader)
if($LASTEXITCODE -ne 0){throw 'Windows GPU query failed; retained collected logs'}
[ordered]@{CollectedAt=(Get-Date).ToString('o');LastBoot=(Get-CimInstance Win32_OperatingSystem).LastBootUpTime.ToString('o');Gpu=$gpu;LogCount=$files.Count;RequiresManualTransactionReview=$true;HardwareUnlockConfirmed=$false} | ConvertTo-Json -Depth 4 | Set-Content (Join-Path $out 'windows-state.json') -Encoding utf8
Write-Output ('Collected logs: '+$files.Count)
Write-Output ('UNLOCK_RESULTS_DIR='+$out)
