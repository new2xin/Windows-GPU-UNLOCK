# GPL-2.0-only. User-authorized USB-first boot; no partition or power operation.
param([Parameter(Mandatory)][string]$BuildDirectory,[switch]$AllowUnverifiedUnlock)
$ErrorActionPreference='Stop'
if(!$AllowUnverifiedUnlock){throw 'This MSI port has not yet proven hardware unlock. Explicit USB-first authorization is required.'}
$build=[IO.Path]::GetFullPath($BuildDirectory)
$m=Get-Content (Join-Path $build 'manifest.json') -Raw | ConvertFrom-Json
if($m.Profile -cne 'MSI-PRO-Z790-P-WIFI-DDR4-1.I0-PALIT-RTX3080TI' -or !$m.CompletePolicyReviewBeforeDetach -or !$m.DependencyScopedGraph -or $m.FixedUsbTopologyRequired -or $m.Mode -cne 'experimental-usb-headless-entry-latched-full'){throw 'Wrong package'}
$media=& (Join-Path $PSScriptRoot 'Resolve-UsbMedia.ps1')
foreach($f in $m.Files){$p=Join-Path $media.Root $f.Path;if((Get-Item -LiteralPath $p).Length -ne $f.Bytes -or (Get-FileHash -LiteralPath $p).Hash -ne $f.Sha256){throw ('Installed package changed: '+$f.Path)}}
if((Get-FileHash (Join-Path $m.PolicyTestsDirectory 'test-results.txt')).Hash -ne $m.PolicyTestResultSha256 -or (Get-FileHash (Join-Path $m.TestsDirectory 'test-results.json')).Hash -ne $m.TestResultSha256){throw 'Test evidence changed'}
if(Confirm-SecureBootUEFI){throw 'Secure Boot enabled'}
$out=Join-Path $PSScriptRoot ('stage/automatic-usb-'+(Get-Date -Format yyyyMMdd_HHmmss_fff))
New-Item -ItemType Directory -Path $out | Out-Null
$reader=Join-Path $PSScriptRoot 'Read-FirmwareBoot.ps1'
$before=& $reader -OutputDirectory (Join-Path $out 'before') | ConvertFrom-Json
if($null -ne $before.BootNext -or $before.BootOrder[0] -ne 'Boot0000'){throw 'Pending one-time boot or unexpected current order'}
$usb=@($before.Entries | Where-Object {$_.Name -eq 'Boot0007' -and $_.Description -ceq 'UEFI: KIOXIA, Partition 1' -and $_.Active -and !$_.Hidden})
if($usb.Count -ne 1){throw 'Expected active firmware USB entry not found'}
function Invoke-PortBcd([string[]]$BcdArguments){$r=@(& bcdedit @BcdArguments);if($LASTEXITCODE -ne 0){throw ('BCDEdit failed: '+($BcdArguments -join ' '))};return ($r -join "`r`n")}
$firmware=Invoke-PortBcd -BcdArguments @('/enum','firmware','/v');$manager=Invoke-PortBcd -BcdArguments @('/enum','{fwbootmgr}','/v')
$match=[regex]::Match($manager,'(?ms)^displayorder\s+(.*?)(?=\r?\n\S|\z)')
if(!$match.Success){throw 'No firmware display order'}
$oldIds=@([regex]::Matches($match.Groups[1].Value,'\{[0-9a-f-]{36}\}') | ForEach-Object {$_.Value})
$winId='{9dea862c-5cdd-4e70-acc1-f32b344d4795}';$usbId='{7433b105-d125-11f1-9f0a-806e6f6e6963}'
if($oldIds.Count -ne $before.BootOrder.Count -or $oldIds[0] -cne $winId -or @($oldIds | Select-Object -Unique).Count -ne $oldIds.Count -or $oldIds -notcontains $usbId){throw 'BCD/native entry order mismatch'}
$target=Invoke-PortBcd -BcdArguments @('/enum',$usbId,'/v')
if(!$media.DriveLetter -or $target -notmatch ('(?m)^device\s+partition='+[regex]::Escape($media.DriveLetter+':')+'\s*$') -or $target -notmatch '(?m)^description\s+UEFI: KIOXIA, Partition 1\s*$'){throw 'BCD USB device identity mismatch'}
[IO.File]::WriteAllText((Join-Path $out 'bcd-firmware-before.txt'),$firmware)
$stamp=Get-Date -Format yyyyMMdd_HHmmss
$null=Invoke-PortBcd -BcdArguments @('/export',(Join-Path $out ('BCD.bak-usb-automatic-'+$stamp)))
$key='HKLM:\SYSTEM\CurrentControlSet\Control\Session Manager\Power'
$power=Get-Item -LiteralPath $key;$exists=$power.GetValueNames() -contains 'HiberbootEnabled'
if($exists -and $power.GetValueKind('HiberbootEnabled') -ne 'DWord'){throw 'Unexpected Fast Startup value type'}
$oldPower=[ordered]@{Exists=$exists;Value=$(if($exists){[int]$power.GetValue('HiberbootEnabled')}else{$null})}
$oldPower | ConvertTo-Json | Set-Content (Join-Path $out 'fast-startup-before.json')
& reg.exe export 'HKLM\SYSTEM\CurrentControlSet\Control\Session Manager\Power' (Join-Path $out ('Power.bak-usb-automatic-'+$stamp+'.reg')) /y | Out-Null
if($LASTEXITCODE -ne 0){throw 'Power registry backup failed'}
$newIds=@($usbId,$winId)+@($oldIds | Where-Object {$_ -ne $usbId -and $_ -ne $winId})
$newOrder=@('Boot0007','Boot0000')+@($before.BootOrder | Where-Object {$_ -ne 'Boot0007' -and $_ -ne 'Boot0000'})
try {
 $null=New-ItemProperty -LiteralPath $key -Name HiberbootEnabled -Value 0 -PropertyType DWord -Force
 if((Get-ItemProperty -LiteralPath $key -Name HiberbootEnabled).HiberbootEnabled -ne 0){throw 'Fast Startup readback failed'}
 $null=Invoke-PortBcd -BcdArguments (@('/set','{fwbootmgr}','displayorder')+$newIds)
 $after=& $reader -OutputDirectory (Join-Path $out 'after') | ConvertFrom-Json
 if(($after.BootOrder -join ',') -cne ($newOrder -join ',') -or $null -ne $after.BootNext){throw 'Native boot priority readback failed'}
 $rawBefore=Get-Content (Join-Path $out 'before/firmware-variables-private.json') -Raw | ConvertFrom-Json
 $rawAfter=Get-Content (Join-Path $out 'after/firmware-variables-private.json') -Raw | ConvertFrom-Json
 foreach($entry in $before.BootOrder){if($rawBefore.$entry.Base64 -cne $rawAfter.$entry.Base64){throw ('Unrelated load option changed: '+$entry)}}
 $state=[ordered]@{Mode='usb-first-automatic';Build=$build;BuildManifestSha256=(Get-FileHash (Join-Path $build 'manifest.json')).Hash;BootOrderBefore=$before.BootOrder;BootOrderAfter=$after.BootOrder;BootNext=$after.BootNext;FastStartupBefore=$oldPower;FastStartupAfter=0;UsbRequired=$true;HardwareUnlockConfirmed=$false;LatestKnownBoot='unlock-skipped';PartitionChanges=$false;PowerOperationPerformed=$false;Stage=$out}
 $state | ConvertTo-Json -Depth 6 | Set-Content (Join-Path $out 'completed.json')
 $state | ConvertTo-Json -Depth 6
}catch{
 $problem=$_
 try{$null=Invoke-PortBcd -BcdArguments (@('/set','{fwbootmgr}','displayorder')+$oldIds)}catch{Write-Warning 'Boot order rollback failed; use F11 Windows Boot Manager or remove USB'}
 try{if($oldPower.Exists){$null=New-ItemProperty -LiteralPath $key -Name HiberbootEnabled -Value $oldPower.Value -PropertyType DWord -Force}else{Remove-ItemProperty -LiteralPath $key -Name HiberbootEnabled}}catch{Write-Warning 'Fast Startup rollback failed; backup is retained'}
 throw $problem
}
