param([string]$StageDirectory)
$ErrorActionPreference='Stop'
if(!$StageDirectory) {
 $active=Join-Path $PSScriptRoot 'stage/active-stage.json'
 $StageDirectory=if(Test-Path -LiteralPath $active){(Get-Content -LiteralPath $active -Raw | ConvertFrom-Json).StageDirectory}else{Join-Path $PSScriptRoot 'stage/20261026_141159_654'}
 if(!$StageDirectory){throw 'Missing active diagnostic stage'}
}
$state=Get-Content -LiteralPath (Join-Path $StageDirectory 'stage.json') -Raw | ConvertFrom-Json
if($state.Mode -ne 'msi-z790-3080ti-observation-only'){throw 'Wrong diagnostic stage'}
$manifest=Get-Content -LiteralPath (Join-Path $state.Build 'manifest.json') -Raw | ConvertFrom-Json
if($manifest.CanUnlock -ne $false -or $manifest.CanDetach -ne $false -or $manifest.EfiSha256 -ne $state.EfiSha256){throw 'Diagnostic capabilities changed'}
$partition=Get-Partition -DiskNumber 0 -PartitionNumber 2
if($partition.Guid -ne $state.EspGuid){throw 'System ESP identity changed'}
if(Confirm-SecureBootUEFI){throw 'Secure Boot is enabled'}
$run=Join-Path $PSScriptRoot ('stage/bootnext-'+(Get-Date -Format 'yyyyMMdd_HHmmss_fff'))
[void][IO.Directory]::CreateDirectory($run)
$reader=Join-Path $PSScriptRoot 'Read-FirmwareBoot.ps1'
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $reader -OutputDirectory (Join-Path $run 'before') > (Join-Path $run 'read-before.log')
if($LASTEXITCODE -ne 0){throw 'Cannot read firmware prerequisites'}
$before=Get-Content -LiteralPath (Join-Path $run 'before/firmware-summary.json') -Raw | ConvertFrom-Json
if($null -ne $before.BootNext){throw 'An existing one-time boot is scheduled; preserve it'}
$target=@($before.Entries | Where-Object {$_.Active -and !$_.Hidden -and $_.FilePaths.Count -eq 1 -and $_.FilePaths[0] -eq $state.EfiPath})
$windows=@($before.Entries | Where-Object {$_.FilePaths -contains '\EFI\MICROSOFT\BOOT\BOOTMGFW.EFI'})
if($target.Count -ne 1 -or $windows.Count -ne 1 -or $before.BootOrder[0] -ne $windows[0].Name){throw 'Unexpected firmware boot identities or Windows priority'}
$letter=@('U','V','W','X','Y','Z') | Where-Object {!(Test-Path ($_+':\')) -and !(Get-PSDrive $_ -ErrorAction SilentlyContinue)} | Select-Object -First 1
if(!$letter){throw 'No unused ESP mount letter'}
$mount=$letter+':';$mounted=$false
try {
 & mountvol $mount /S
 if($LASTEXITCODE -ne 0){throw 'ESP mount failed'}
 $mounted=$true
 $volume=(@(& mountvol $mount /L) -join '').Trim()
 if($LASTEXITCODE -ne 0 -or $partition.AccessPaths.Count -ne 1 -or $volume -ne $partition.AccessPaths[0]){throw 'Mounted ESP mismatch'}
 $installed=Join-Path ($mount+'\') $state.EfiPath.TrimStart('\')
 if((Get-FileHash -LiteralPath $installed).Hash -ne $state.EfiSha256){throw 'Installed diagnostic changed'}
} finally {if($mounted){& mountvol $mount /D;if($LASTEXITCODE -ne 0){throw 'Temporary ESP mount could not be removed'}}}
& bcdedit /export (Join-Path $run 'BCD.bak-bootnext')
if($LASTEXITCODE -ne 0){throw 'BCD backup failed'}
@(& bcdedit /enum firmware /v) | Set-Content (Join-Path $run 'bcd-firmware-before.txt') -Encoding UTF8
if($LASTEXITCODE -ne 0){throw 'BCD firmware snapshot failed'}
& bcdedit /set '{fwbootmgr}' bootsequence $state.Entry
if($LASTEXITCODE -ne 0){throw 'One-time boot selection failed'}
try {
 & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $reader -OutputDirectory (Join-Path $run 'after') > (Join-Path $run 'read-after.log')
 if($LASTEXITCODE -ne 0){throw 'Firmware readback failed'}
 $after=Get-Content -LiteralPath (Join-Path $run 'after/firmware-summary.json') -Raw | ConvertFrom-Json
 if($after.BootNext -ne $target[0].Name -or ($after.BootOrder -join ',') -ne ($before.BootOrder -join ',')){throw 'BootNext or persistent BootOrder verification failed'}
 $rawBefore=Get-Content (Join-Path $run 'before/firmware-variables-private.json') -Raw | ConvertFrom-Json
 $rawAfter=Get-Content (Join-Path $run 'after/firmware-variables-private.json') -Raw | ConvertFrom-Json
 foreach($name in $before.BootOrder){if($rawBefore.$name.Base64 -ne $rawAfter.$name.Base64){throw "Unexpected load-option change: $name"}}
 [ordered]@{Mode='one-time-observer-boot';BootNext=$after.BootNext;PersistentBootOrder=$after.BootOrder;DefaultWindowsUnchanged=$true;RebootRequested=$false;EfiPath=$state.EfiPath;EfiSha256=$state.EfiSha256;Entry=$state.Entry;RecordedUtc=[DateTime]::UtcNow.ToString('o')} | ConvertTo-Json -Depth 4 | Set-Content (Join-Path $run 'plan.json') -Encoding UTF8
 Write-Output "Verified BootNext=$($after.BootNext); persistent Windows-first order unchanged. No shutdown or restart requested. Record: $run"
} catch {
 $problem=$_
 & bcdedit /deletevalue '{fwbootmgr}' bootsequence
 if($LASTEXITCODE -ne 0){Write-Warning 'Could not cancel unverified one-time selection; inspect firmware state'}
 throw $problem
}
