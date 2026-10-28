param([string]$OutputDirectory)
$ErrorActionPreference='Stop'
Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
public static class PortFirmwareReader {
 [StructLayout(LayoutKind.Sequential)] struct LUID {public uint LowPart;public int HighPart;}
 [StructLayout(LayoutKind.Sequential)] struct TOKEN_PRIVILEGES {public uint Count;public LUID Luid;public uint Attributes;}
 [DllImport("kernel32.dll")] static extern IntPtr GetCurrentProcess();
 [DllImport("advapi32.dll",SetLastError=true)] static extern bool OpenProcessToken(IntPtr p,uint access,out IntPtr token);
 [DllImport("advapi32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern bool LookupPrivilegeValue(string system,string name,out LUID luid);
 [DllImport("advapi32.dll",SetLastError=true)] static extern bool AdjustTokenPrivileges(IntPtr token,bool disable,ref TOKEN_PRIVILEGES state,uint length,IntPtr previous,IntPtr returned);
 [DllImport("kernel32.dll",SetLastError=true)] static extern bool CloseHandle(IntPtr h);
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern uint GetFirmwareEnvironmentVariableEx(string name,string guid,byte[] buffer,uint size,out uint attributes);
 public static void EnableReadPrivilege() {
  IntPtr token;if(!OpenProcessToken(GetCurrentProcess(),0x28,out token))throw new Win32Exception();
  try {LUID luid;if(!LookupPrivilegeValue(null,"SeSystemEnvironmentPrivilege",out luid))throw new Win32Exception();
   TOKEN_PRIVILEGES s=new TOKEN_PRIVILEGES{Count=1,Luid=luid,Attributes=2};
   if(!AdjustTokenPrivileges(token,false,ref s,0,IntPtr.Zero,IntPtr.Zero))throw new Win32Exception();
   int e=Marshal.GetLastWin32Error();if(e!=0)throw new Win32Exception(e);
  } finally {CloseHandle(token);}
 }
 public static byte[] Read(string name,out uint attributes) {
  byte[] b=new byte[65536];uint n=GetFirmwareEnvironmentVariableEx(name,"{8be4df61-93ca-11d2-aa0d-00e098032b8c}",b,(uint)b.Length,out attributes);
  if(n==0){int e=Marshal.GetLastWin32Error();if(e==203)return null;throw new Win32Exception(e);}
  Array.Resize(ref b,(int)n);return b;
 }
}
'@
[PortFirmwareReader]::EnableReadPrivilege()
$raw=[ordered]@{}
function Read-Variable([string]$Name) {
 [uint32]$attrs=0
 $data=[PortFirmwareReader]::Read($Name,[ref]$attrs)
 if($null -ne $data){$raw[$Name]=[ordered]@{Attributes=$attrs;Base64=[Convert]::ToBase64String($data)}}
 return ,$data
}
$order=Read-Variable 'BootOrder'
if(!$order -or $order.Length%2){throw 'Invalid firmware BootOrder'}
$ids=@();for($i=0;$i -lt $order.Length;$i+=2){$ids+=[BitConverter]::ToUInt16($order,$i)}
$summary=[ordered]@{BootOrder=@($ids | ForEach-Object {'Boot{0:X4}' -f $_});BootCurrent=$null;BootNext=$null;Entries=@()}
foreach($name in @('BootCurrent','BootNext')){$b=Read-Variable $name;if($b -and $b.Length -eq 2){$summary[$name]='Boot{0:X4}' -f [BitConverter]::ToUInt16($b,0)}}
foreach($id in @($ids+(0..63) | Select-Object -Unique)) {
 $name='Boot{0:X4}' -f $id;$b=Read-Variable $name;if(!$b){continue}
 if($b.Length -lt 8){throw "Malformed $name"}
 $attrs=[BitConverter]::ToUInt32($b,0);$pathBytes=[BitConverter]::ToUInt16($b,4);$end=6
 while($end+1 -lt $b.Length -and ($b[$end] -ne 0 -or $b[$end+1] -ne 0)){$end+=2}
 if($end+1 -ge $b.Length -or $end+2+$pathBytes -gt $b.Length){throw "Invalid bounds in $name"}
 $description=[Text.Encoding]::Unicode.GetString($b,6,$end-6)
 $at=$end+2;$pathEnd=$at+$pathBytes;$paths=@()
 while($at+4 -le $pathEnd) {
   $n=[BitConverter]::ToUInt16($b,$at+2)
   if($n -lt 4 -or $n -gt $pathEnd-$at){throw "Malformed device path in $name"}
   if($b[$at] -eq 4 -and $b[$at+1] -eq 4){$paths += [Text.Encoding]::Unicode.GetString($b,$at+4,$n-4).TrimEnd([char]0)}
   $at+=$n
 }
 $summary.Entries += [ordered]@{Name=$name;InBootOrder=($ids -contains $id);Description=$description;LoadOptionAttributes=('0x{0:X8}' -f $attrs);Active=[bool]($attrs-band 1);Hidden=[bool]($attrs-band 8);Category=('0x{0:X4}' -f ($attrs-band 0x1f00));FilePaths=$paths;PathBytes=$pathBytes;OptionalBytes=($b.Length-$pathEnd)}
}
if($OutputDirectory) {
 if(Test-Path -LiteralPath $OutputDirectory){throw 'Output already exists'}
 [void][IO.Directory]::CreateDirectory($OutputDirectory)
 $raw | ConvertTo-Json -Depth 5 | Set-Content (Join-Path $OutputDirectory 'firmware-variables-private.json') -Encoding UTF8
 $summary | ConvertTo-Json -Depth 6 | Set-Content (Join-Path $OutputDirectory 'firmware-summary.json') -Encoding UTF8
}
$summary | ConvertTo-Json -Depth 6
