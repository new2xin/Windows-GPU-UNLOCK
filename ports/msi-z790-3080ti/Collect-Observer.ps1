param([string]$StageDirectory)
$ErrorActionPreference='Stop'
if(!$StageDirectory) {
 $active=Join-Path $PSScriptRoot 'stage/active-stage.json'
 $StageDirectory=if(Test-Path -LiteralPath $active){(Get-Content -LiteralPath $active -Raw | ConvertFrom-Json).StageDirectory}else{Join-Path $PSScriptRoot 'stage/20261026_141159_654'}
 if(!$StageDirectory){throw 'Missing active diagnostic stage'}
}
$state=Get-Content -LiteralPath (Join-Path $StageDirectory 'stage.json') -Raw | ConvertFrom-Json
if($state.Mode -ne 'msi-z790-3080ti-observation-only'){throw 'Unexpected stage identity'}
$partition=Get-Partition -DiskNumber 0 -PartitionNumber 2
if($partition.Guid -ne $state.EspGuid){throw 'System ESP identity changed'}
$letter=@('U','V','W','X','Y','Z') | Where-Object {!(Test-Path ($_+':\')) -and !(Get-PSDrive $_ -ErrorAction SilentlyContinue)} | Select-Object -First 1
if(!$letter){throw 'No unused mount letter'}
$mount=$letter+':';$mounted=$false
try {
 & mountvol $mount /S
 if($LASTEXITCODE -ne 0){throw 'ESP mount failed'}
 $mounted=$true
 $mountedVolume=(@(& mountvol $mount /L) -join '').Trim()
 if($LASTEXITCODE -ne 0 -or $partition.AccessPaths.Count -ne 1 -or $mountedVolume -ne $partition.AccessPaths[0]){throw 'Mounted ESP mismatch'}
 $installed=Join-Path ($mount+'\') $state.EfiPath.TrimStart('\')
 if((Get-FileHash -LiteralPath $installed).Hash -ne $state.EfiSha256){throw 'Installed observer changed'}
 $source=Join-Path ($mount+'\') 'GPU-PORT'
 if(!(Test-Path -LiteralPath $source)){Write-Output 'No capture yet. Boot the manual diagnostic entry first.';exit 2}
 $result=Join-Path $PSScriptRoot ('results/'+(Get-Date -Format 'yyyyMMdd_HHmmss_fff'))
 [void][IO.Directory]::CreateDirectory($result)
 $copied=Join-Path $result 'GPU-PORT'
 & robocopy $source $copied /E /COPY:DT /DCOPY:T /R:0 /W:0 /XJ /NFL /NDL /NJH /NJS > (Join-Path $result 'copy.log')
 if($LASTEXITCODE -ge 8){throw 'Capture copy failed'}
 $files=@(Get-ChildItem -LiteralPath $copied -File -Recurse -Force)
 $files | ForEach-Object {
   $relative=$_.FullName.Substring($copied.Length+1)
   $hash=(Get-FileHash -LiteralPath $_.FullName).Hash
   if((Get-FileHash -LiteralPath (Join-Path $source $relative)).Hash -ne $hash){throw "Capture copy mismatch: $relative"}
   [ordered]@{Path=$relative;Bytes=$_.Length;SHA256=$hash}
 } | ConvertTo-Json -Depth 3 | Set-Content (Join-Path $result 'capture-manifest.json') -Encoding UTF8
 $failures=0
 foreach($run in Get-ChildItem -LiteralPath $copied -Directory | Where-Object {$_.Name -match '^RUN\d{3}$'}) {
   & python (Join-Path $PSScriptRoot 'analyze_port.py') $run.FullName --output (Join-Path $result ($run.Name+'-candidate.json'))
   if($LASTEXITCODE -ne 0){$failures++}
 }
 Write-Output "Private captures copied and hash verified: $result. No files uploaded."
 if($failures){exit 2}
} finally {
 if($mounted){& mountvol $mount /D;if($LASTEXITCODE -ne 0){Write-Warning "Temporary mount remains at $mount"}}
}
