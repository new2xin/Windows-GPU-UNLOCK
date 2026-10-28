# GPL-2.0-only. Independent recovery; needs neither USB nor build artifacts.
param([switch]$RestoreFastStartup)
$ErrorActionPreference='Stop'
$out=Join-Path $PSScriptRoot ('stage/windows-first-'+(Get-Date -Format yyyyMMdd_HHmmss_fff))
New-Item -ItemType Directory -Path $out | Out-Null
$stamp=Get-Date -Format yyyyMMdd_HHmmss
& bcdedit /export (Join-Path $out ('BCD.bak-windows-first-'+$stamp))
if($LASTEXITCODE -ne 0){throw 'BCD backup failed'}
& bcdedit /set '{fwbootmgr}' displayorder '{bootmgr}' /addfirst
if($LASTEXITCODE -ne 0){throw 'Windows-first restoration failed'}
$r=& (Join-Path $PSScriptRoot 'Read-FirmwareBoot.ps1') -OutputDirectory (Join-Path $out 'after') | ConvertFrom-Json
if($r.BootOrder[0] -cne 'Boot0000'){throw 'Windows-first native readback failed'}
if($RestoreFastStartup){
 $saved=Get-ChildItem (Join-Path $PSScriptRoot 'stage') -Directory -Filter 'automatic-usb-*' | Sort-Object Name -Descending | Where-Object {Test-Path (Join-Path $_.FullName 'completed.json')} | Select-Object -First 1
 if(!$saved){throw 'No completed automatic configuration found; Windows priority is already restored'}
 $old=Get-Content (Join-Path $saved.FullName 'fast-startup-before.json') -Raw | ConvertFrom-Json
 $key='HKLM:\SYSTEM\CurrentControlSet\Control\Session Manager\Power'
 & reg.exe export 'HKLM\SYSTEM\CurrentControlSet\Control\Session Manager\Power' (Join-Path $out ('Power.bak-windows-first-'+$stamp+'.reg')) /y | Out-Null
 if($LASTEXITCODE -ne 0){throw 'Power registry backup failed'}
 if($old.Exists){$null=New-ItemProperty -LiteralPath $key -Name HiberbootEnabled -Value ([int]$old.Value) -PropertyType DWord -Force}
 elseif((Get-Item -LiteralPath $key).GetValueNames() -contains 'HiberbootEnabled'){Remove-ItemProperty -LiteralPath $key -Name HiberbootEnabled}
 $power=Get-Item -LiteralPath $key
 if($old.Exists){if($power.GetValueKind('HiberbootEnabled') -ne 'DWord' -or [int]$power.GetValue('HiberbootEnabled') -ne [int]$old.Value){throw 'Fast Startup restoration readback failed'}}
 elseif($power.GetValueNames() -contains 'HiberbootEnabled'){throw 'Fast Startup value removal readback failed'}
}
Write-Output ('Windows Boot Manager first verified. No reboot. Record: '+$out)
