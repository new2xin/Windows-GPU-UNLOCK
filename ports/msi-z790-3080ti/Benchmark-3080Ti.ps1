# GPL-2.0-only. Isolated ordinary CUDA benchmark; no GPU/driver setting changes.
param([Parameter(Mandatory)][string]$BootLog,[ValidateSet('stock-unlock-skipped','verified-unlock')][string]$StateLabel='stock-unlock-skipped')
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$boot=[IO.Path]::GetFullPath($BootLog)
$text=Get-Content -LiteralPath $boot -Raw
if($StateLabel -eq 'stock-unlock-skipped' -and $text -notmatch 'full-unlock-skipped-no-driver-release status=0x0000000000000000'){throw 'No skipped-unlock evidence'}
if($StateLabel -eq 'verified-unlock' -and ($text -notmatch 'original-unlock-return status=0x0000000000000000' -or $text -notmatch 'full-windows-start status=0x0000000000000000' -or $text -match 'full-unlock-skipped')){throw 'No successful unlock/handoff evidence'}
$cuda='C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v12.6'
$vcvars='C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat'
$nvcc=Join-Path $cuda 'bin/nvcc.exe'
$out=Join-Path $PSScriptRoot ('results/benchmark-'+$StateLabel+'-'+(Get-Date -Format yyyyMMdd_HHmmss_fff))
New-Item -ItemType Directory -Path $out | Out-Null
$source=Join-Path $repo 'benchmark/compute-benchmark-3090.cu'
$generated=Join-Path $out 'compute-benchmark-3080ti.cu'
$original=[IO.File]::ReadAllText($source)
if(($original.Split('NVIDIA GeForce RTX 3090').Count-1) -ne 1 -or ($original.Split('Exact RTX 3090').Count-1) -ne 1){throw 'Unexpected original benchmark selection'}
[IO.File]::WriteAllText($generated,$original.Replace('NVIDIA GeForce RTX 3090','NVIDIA GeForce RTX 3080 Ti').Replace('Exact RTX 3090','Exact RTX 3080 Ti'))
$environment=& cmd.exe /d /c "call `"$vcvars`" >nul && set"
if($LASTEXITCODE -ne 0){throw 'Compiler environment failed'}
foreach($line in $environment){if($line -match '^([^=]+)=(.*)$'){[Environment]::SetEnvironmentVariable($matches[1],$matches[2],'Process')}}
$env:PATH=(Join-Path $cuda 'bin')+';'+$env:PATH
$exe=Join-Path $out 'compute-benchmark-3080ti.exe'
& $nvcc -allow-unsupported-compiler -std=c++17 -O3 -arch=sm_86 -Xcompiler /utf-8 $generated -lcublas -o $exe *> (Join-Path $out 'compile.log')
if($LASTEXITCODE -ne 0){throw 'Benchmark compile failed; retain compile.log'}
$inspect=& $exe --inspect-only
if($LASTEXITCODE -ne 0){throw 'Runtime inspect failed'}
$runtime=$inspect | ConvertFrom-Json
if($runtime.cuda_runtime -ne 12060 -or $runtime.cublas_major -ne 12 -or $runtime.cublas_minor -ne 6){throw 'Unexpected existing CUDA/cuBLAS runtime'}
function Gpu {
 $rows=@(& nvidia-smi --query-gpu=name,uuid,pci.bus_id,driver_version,utilization.gpu,memory.used,temperature.gpu,power.limit,clocks.max.sm,clocks.max.memory --format=csv,noheader,nounits)
 if($LASTEXITCODE -ne 0 -or $rows.Count -ne 1){throw 'GPU inventory failed'}
 $f=@($rows[0].Split(',') | ForEach-Object {$_.Trim()})
 if($f[0] -cne 'NVIDIA GeForce RTX 3080 Ti' -or $f[1] -cne 'GPU-00000000-0000-0000-0000-000000000001' -or $f[2] -notmatch '^0+:01:00\.0$'){throw 'Exact GPU changed'}
 return ,$f
}
$before=Gpu
if([int]$before[4] -gt 5 -or [int]$before[5] -gt 2048 -or [int]$before[6] -gt 65){throw 'GPU busy or hot; nothing was stopped'}
$before -join ',' | Set-Content (Join-Path $out 'gpu-before.csv')
$files=@($source,$generated,$exe,$nvcc,(Join-Path $cuda 'bin/cublas64_12.dll'),(Join-Path $cuda 'bin/cublasLt64_12.dll'),$boot)
$provenance=[ordered]@{StateLabel=$StateLabel;BootLog=$boot;Runtime=$runtime;CompilerCompatibilityOverride=$true;Driver=$before[3];GpuUuid=$before[1];Files=@($files | ForEach-Object {[ordered]@{Path=$_;Bytes=(Get-Item -LiteralPath $_).Length;Sha256=(Get-FileHash -LiteralPath $_).Hash}})}
$provenance | ConvertTo-Json -Depth 6 | Set-Content (Join-Path $out 'provenance.json') -Encoding utf8
$trials=@()
for($i=1;$i -le 3;$i++){
 $json=& $exe 2> (Join-Path $out ('trial-'+$i+'.stderr.txt'))
 if($LASTEXITCODE -ne 0){throw ('Benchmark failed: trial '+$i)}
 $r=$json | ConvertFrom-Json
 if($r.gpu -cne $before[0] -or $r.cuda_runtime -ne 12060 -or $r.benchmarks.Count -ne 3 -or @($r.benchmarks | Where-Object {!$_.reference_checked}).Count){throw 'Unexpected benchmark output'}
 $json | Set-Content (Join-Path $out ('trial-'+$i+'.json')) -Encoding utf8
 $trials+=$r
 & nvidia-smi --query-gpu=timestamp,driver_version,clocks.sm,power.draw,temperature.gpu,clocks_event_reasons.sw_thermal_slowdown --format=csv | Set-Content (Join-Path $out ('trial-'+$i+'-telemetry.csv'))
 if($LASTEXITCODE -ne 0){throw 'GPU telemetry failed'}
 $after=Gpu
 foreach($at in @(0,1,2,3,7,8,9)){if($before[$at] -cne $after[$at]){throw 'Comparison settings changed'}}
}
$summary=@(foreach($name in @('fp32_pedantic','fp16_tensor_fp32_accumulate','bf16_tensor_fp32_accumulate')){
 $v=@($trials | ForEach-Object {$_.benchmarks | Where-Object name -eq $name | ForEach-Object {[double]$_.gflops/1000}} | Sort-Object)
 [ordered]@{Name=$name;MedianTflops=$v[1];MinTflops=$v[0];MaxTflops=$v[2]}
})
[ordered]@{StateLabel=$StateLabel;Trials=3;Summary=$summary;UnlockSpeedupClaimed=$false;FinishedAt=(Get-Date).ToString('o')} | ConvertTo-Json -Depth 5 | Set-Content (Join-Path $out 'summary.json') -Encoding utf8
$summary | ForEach-Object {[pscustomobject]$_} | Format-Table -AutoSize
Write-Output ('BENCHMARK_DIR='+$out)
