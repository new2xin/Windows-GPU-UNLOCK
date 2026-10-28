/* SPDX-License-Identifier: GPL-2.0-only. Real observer behind simulated EFI APIs.
 * No real device/filesystem access; dangerous EFI services and PCI writes are NULL. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#undef NULL
#include "main.c"
#include <Protocol/DriverBinding.h>
static int Checks,Scenario,Reads,ConfigReads,Saves,Creates,Closed,Loads,Starts,Captures,Flushes,Stalls;
static EFI_BOOT_SERVICES Bs;
static EFI_RUNTIME_SERVICES Rt;
static EFI_SYSTEM_TABLE Table;
static EFI_PCI_IO_PROTOCOL Pci;
static EFI_GUID PciGuid=EFI_PCI_IO_PROTOCOL_GUID;
static EFI_LOADED_IMAGE_PROTOCOL Loaded,DriverImage;
static EFI_DRIVER_BINDING_PROTOCOL Binding;
static EFI_SIMPLE_FILE_SYSTEM_PROTOCOL Fs;
static EFI_FILE_PROTOCOL File;
static EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL Console;
static char Screen[8192];static UINTN ScreenUsed;
static char Output[65536];static UINTN OutputUsed;
static UINT8 ImageBytes[512];
static struct {HARDDRIVE_DEVICE_PATH Hd;EFI_DEVICE_PATH_PROTOCOL End;} DevicePath;
enum {OK,NO_TARGET,DUPLICATE,READ_FAIL,BAD_SUBSYSTEM,BAD_BDF,BAD_BAR,CHANGED,MMIO_FAIL,PROM_FAIL,PROM_ZERO,COLLISION,SHORT_WRITE,NO_SPACE,INFO_FAIL,MISSING_FS,WATCH_FAIL,TRACE_SHORT,TRACE_FLUSH_FAIL,LOG_FAIL,MUTATE_IMAGE};
static void Check(int ok,const char *s){Checks++;if(!ok){fprintf(stderr,"FAIL scenario=%d: %s\n",Scenario,s);exit(1);}}
VOID Text(CAPTURE *c,const char *s){size_t n=strlen(s);Check(OutputUsed+n<sizeof(Output),"log bound");memcpy(Output+OutputUsed,s,n+1);OutputUsed+=n;(void)c;}
VOID Hex(CAPTURE *c,UINT64 v,UINTN n){char s[20];snprintf(s,sizeof(s),"%0*llx",(int)n,(unsigned long long)v);Text(c,s);}
VOID End(CAPTURE *c){Text(c,"\n");}
VOID Problem(CAPTURE *c,const char *s,EFI_STATUS status){c->Complete=FALSE;Text(c,s);End(c);(void)status;}
EFI_STATUS GuCaptureGraph(EFI_SYSTEM_TABLE *t,EFI_FILE_PROTOCOL *f){(void)t;(void)f;Check(Saves==4&&Flushes==4,"bootstrap proof persisted before graph/GPU access");Captures++;return EFI_NOT_READY;}
static EFI_STATUS EFIAPI Allocate(EFI_MEMORY_TYPE t,UINTN n,VOID **p){(void)t;*p=calloc(1,n);return *p?EFI_SUCCESS:EFI_OUT_OF_RESOURCES;}
static EFI_STATUS EFIAPI Free(VOID *p){free(p);return EFI_SUCCESS;}
static EFI_STATUS EFIAPI Close(EFI_FILE_PROTOCOL *f){(void)f;Closed++;return EFI_SUCCESS;}
static EFI_STATUS EFIAPI Flush(EFI_FILE_PROTOCOL *f){(void)f;Flushes++;return Scenario==TRACE_FLUSH_FAIL?EFI_DEVICE_ERROR:EFI_SUCCESS;}
static EFI_STATUS EFIAPI Write(EFI_FILE_PROTOCOL *f,UINTN *n,VOID *p){
  (void)f;Saves++;
  if(Scenario==MUTATE_IMAGE&&*n==sizeof(ImageBytes)) {
    Check(ImageBytes[0]==0xa5,"original image really changed during filesystem callback");
    Check(p!=ImageBytes,"save uses private snapshot");
    for(UINTN i=0;i<*n;i++)Check(((UINT8 *)p)[i]==0x5a,"saved snapshot stays identical to hashed bytes");
  }
  if((Scenario==SHORT_WRITE||Scenario==TRACE_SHORT)&&*n)(*n)--;return EFI_SUCCESS;
}
static int WideEqual(const CHAR16 *a,const CHAR16 *b){while(*a&&*a==*b){a++;b++;}return *a==*b;}
static EFI_STATUS EFIAPI Open(EFI_FILE_PROTOCOL *f,EFI_FILE_PROTOCOL **out,CHAR16 *name,UINT64 mode,UINT64 attrs) {
  (void)f;(void)attrs;
  if(Scenario==MUTATE_IMAGE&&WideEqual(name,L"IMG000.BIN"))memset(ImageBytes,0xa5,sizeof(ImageBytes));
  if(mode==EFI_FILE_MODE_READ) {
    if(Scenario==COLLISION||WideEqual(name,L"GPU-PORT")){*out=&File;return EFI_SUCCESS;}
    *out=NULL;return EFI_NOT_FOUND;
  }
  Check(mode==(EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE|EFI_FILE_MODE_CREATE),"only explicit new file opens");
  if(Scenario==LOG_FAIL&&WideEqual(name,L"CAPTURE.TXT")){*out=NULL;return EFI_ACCESS_DENIED;}
  Creates++;*out=&File;return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Info(EFI_FILE_PROTOCOL *f,EFI_GUID *g,UINTN *n,VOID *data) {
  if(Scenario==INFO_FAIL)return EFI_UNSUPPORTED;
  (void)f;(void)g;if(*n<sizeof(EFI_FILE_SYSTEM_INFO))return EFI_BUFFER_TOO_SMALL;
  EFI_FILE_SYSTEM_INFO *i=data;memset(i,0,sizeof(*i));i->Size=sizeof(*i);i->FreeSpace=Scenario==NO_SPACE?0:64*1024*1024;*n=sizeof(*i);return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Volume(EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs,EFI_FILE_PROTOCOL **f){(void)fs;*f=&File;return EFI_SUCCESS;}
static EFI_STATUS EFIAPI PciRead(EFI_PCI_IO_PROTOCOL *p,EFI_PCI_IO_PROTOCOL_WIDTH w,UINT32 off,UINTN n,VOID *data) {
  (void)p;Check(w==EfiPciIoWidthUint32&&off==0&&(n==1||n==64),"bounded PCI reads");
  UINT32 *cfg=data;if(n==1){*cfg=Scenario==NO_TARGET?0x123410de:PORT_PCI_ID;return EFI_SUCCESS;}
  ConfigReads++;memset(cfg,0,n*4);cfg[0]=PORT_PCI_ID;cfg[1]=2;cfg[2]=0x030000a1;cfg[4]=0xf5000000;cfg[11]=PORT_SUBSYSTEM;
  if(Scenario==READ_FAIL)return EFI_DEVICE_ERROR;
  if(Scenario==BAD_SUBSYSTEM)cfg[11]^=1;
  if(Scenario==CHANGED&&ConfigReads==2)cfg[4]^=0x1000000;
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Location(EFI_PCI_IO_PROTOCOL *p,UINTN *s,UINTN *b,UINTN *d,UINTN *f){(void)p;*s=0;*b=Scenario==BAD_BDF?2:1;*d=*f=0;return EFI_SUCCESS;}
static EFI_STATUS EFIAPI Bar(EFI_PCI_IO_PROTOCOL *p,UINT8 index,UINT64 *supports,VOID **out) {
  (void)p;Check(index==0,"only BAR0");*supports=0;EFI_ACPI_ADDRESS_SPACE_DESCRIPTOR *r=calloc(1,sizeof(*r));*out=r;
  r->Desc=ACPI_ADDRESS_SPACE_DESCRIPTOR;r->Len=0x2b;r->ResType=ACPI_ADDRESS_SPACE_TYPE_MEM;
  r->AddrRangeMin=0xf5000000;r->AddrRangeMax=0xf5ffffff;r->AddrLen=Scenario==BAD_BAR?1:0x1000000;return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Mem(EFI_PCI_IO_PROTOCOL *p,EFI_PCI_IO_PROTOCOL_WIDTH w,UINT8 bar,UINT64 off,UINTN n,VOID *data) {
  (void)p;Check(w==EfiPciIoWidthUint32&&bar==0,"ordinary BAR0 read");Reads++;
  if(Scenario==MMIO_FAIL)return EFI_DEVICE_ERROR;
  if(off>=0x300000&&off<0x400000) {
    Check(n==1024&&off+4*n<=0x400000,"PROM reads stay in 1 MiB window");
    if(Scenario==PROM_FAIL)return EFI_DEVICE_ERROR;
    for(UINTN i=0;i<n;i++)((UINT32 *)data)[i]=Scenario==PROM_ZERO?0:(UINT32)(off+4*i);return EFI_SUCCESS;
  }
  Check(n==1&&off+4<=0x1000000,"bounded register read");*(UINT32 *)data=off==0?0xb72000a1:off==0x1183a4?0x3000:0;return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Locate(EFI_LOCATE_SEARCH_TYPE type,EFI_GUID *g,VOID *key,UINTN *n,EFI_HANDLE **out) {
  (void)type;(void)key;UINTN count=Equal(g,&PciGuid)&&Scenario==DUPLICATE?2:1;
  *out=malloc(count*sizeof(**out));*n=count;for(UINTN i=0;i<count;i++)(*out)[i]=(VOID *)1;return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Handle(EFI_HANDLE h,EFI_GUID *g,VOID **out) {
  (void)h;EFI_GUID bind=EFI_DRIVER_BINDING_PROTOCOL_GUID;
  if(Equal(g,&PciGuid))*out=&Pci;
  else if(Equal(g,&bind))*out=&Binding;
  else if(Equal(g,&LoadedGuid))*out=h==(VOID *)2?&DriverImage:&Loaded;
  else if(Equal(g,&FsGuid)){if(Scenario==MISSING_FS)return EFI_UNSUPPORTED;*out=&Fs;}
  else if(Equal(g,&PathGuid))*out=&DevicePath;
  else return EFI_NOT_FOUND;
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Load(BOOLEAN policy,EFI_HANDLE parent,EFI_DEVICE_PATH_PROTOCOL *path,VOID *data,UINTN n,EFI_HANDLE *image) {
  (void)policy;(void)parent;Check(!data&&!n,"Windows loaded by explicit device path");
  UINT8 *file=(UINT8 *)path+sizeof(HARDDRIVE_DEVICE_PATH);
  Check(file[0]==MEDIA_DEVICE_PATH&&file[1]==MEDIA_FILEPATH_DP,"Windows file path node");
  Check(WideEqual((VOID *)(file+4),L"\\EFI\\Microsoft\\Boot\\bootmgfw.efi"),"only original Windows loader");
  Loads++;*image=(VOID *)3;return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Start(EFI_HANDLE image,UINTN *n,CHAR16 **data){(void)n;(void)data;Check(image==(VOID *)3,"only Windows started");Starts++;return EFI_SUCCESS;}
static EFI_STATUS EFIAPI Watch(UINTN t,UINT64 code,UINTN n,CHAR16 *data){(void)code;(void)n;(void)data;Check(t==0,"disable app watchdog only");return Scenario==WATCH_FAIL?EFI_UNSUPPORTED:EFI_SUCCESS;}
static EFI_STATUS EFIAPI Stall(UINTN usec){Check(usec==5000000||usec==10000000,"bounded visible status delay");Stalls++;return EFI_SUCCESS;}
static EFI_STATUS EFIAPI Print(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *out,CHAR16 *s) {
  (void)out;while(*s){Check(ScreenUsed+1<sizeof(Screen),"console output bound");Screen[ScreenUsed++]=(char)*s++;}Screen[ScreenUsed]=0;return EFI_SUCCESS;
}
static void Setup(int scenario) {
  Scenario=scenario;Reads=ConfigReads=Saves=Creates=Closed=Loads=Starts=Captures=Flushes=Stalls=0;OutputUsed=ScreenUsed=0;Output[0]=Screen[0]=0;
  memset(&Bs,0,sizeof(Bs));memset(&Rt,0,sizeof(Rt));memset(&Table,0,sizeof(Table));memset(&Pci,0,sizeof(Pci));memset(&File,0,sizeof(File));
  Bs.AllocatePool=Allocate;Bs.FreePool=Free;Bs.LocateHandleBuffer=Locate;Bs.HandleProtocol=Handle;Bs.LoadImage=Load;Bs.StartImage=Start;Bs.SetWatchdogTimer=Watch;Bs.Stall=Stall;
  Console.OutputString=Print;Table.ConOut=&Console;Table.BootServices=&Bs;Table.RuntimeServices=&Rt;
  Pci.Pci.Read=PciRead;Pci.Mem.Read=Mem;Pci.GetLocation=Location;Pci.GetBarAttributes=Bar;
  File.Open=Open;File.Close=Close;File.Write=Write;File.Flush=Flush;File.GetInfo=Info;
  Fs.OpenVolume=Volume;Loaded.DeviceHandle=(VOID *)1;DriverImage.ImageBase=ImageBytes;DriverImage.ImageSize=sizeof(ImageBytes);
  Binding.ImageHandle=(VOID *)2;
  memset(&DevicePath,0,sizeof(DevicePath));DevicePath.Hd.Header.Type=MEDIA_DEVICE_PATH;DevicePath.Hd.Header.SubType=MEDIA_HARDDRIVE_DP;DevicePath.Hd.Header.Length[0]=sizeof(HARDDRIVE_DEVICE_PATH);
  DevicePath.Hd.PartitionSize=1024;DevicePath.Hd.MBRType=2;DevicePath.Hd.SignatureType=2;memcpy(DevicePath.Hd.Signature,&WindowsGuid,16);
  DevicePath.End.Type=END_DEVICE_PATH_TYPE;DevicePath.End.SubType=END_ENTIRE_DEVICE_PATH_SUBTYPE;DevicePath.End.Length[0]=4;
  PortSetDirectory(&File);
}
int main(void) {
  for(int scenario=OK;scenario<=SHORT_WRITE;scenario++) {
    Setup(scenario);CAPTURE c={0};c.Table=&Table;c.Complete=TRUE;c.Io=EFI_SUCCESS;
    EFI_STATUS s=GuProbeGpu(&Table,&c);
    if(scenario==OK){Check(s==EFI_SUCCESS&&c.Complete,"complete observation");Check(Saves==2&&Creates==2,"PROM plus one unique driver image");}
    else Check(s!=EFI_SUCCESS||!c.Complete,"failure stays incomplete");
    if(scenario>=NO_TARGET&&scenario<=CHANGED)Check(Reads==0&&Saves==0,"no MMIO after identity/resource failure");
    if(scenario==COLLISION)Check(Creates==0&&Saves==0,"never overwrite an existing capture");
    Check(!Pci.Pci.Write&&!Pci.Mem.Write&&!Pci.Attributes&&!Pci.Map&&!Bs.DisconnectController&&!Rt.SetVariable,"no unsafe services supplied");
  }
  Setup(OK);UINTN size=0;BOOLEAN windows=FALSE;
  Check(Inspect((VOID *)&DevicePath,&size,&windows)&&windows&&size==sizeof(DevicePath),"pinned Windows partition accepted");
  DevicePath.Hd.Signature[0]^=1;Check(Inspect((VOID *)&DevicePath,&size,&windows)&&!windows,"wrong ESP never matched");
  DevicePath.End.SubType=END_INSTANCE_DEVICE_PATH_SUBTYPE;Check(!Inspect((VOID *)&DevicePath,&size,&windows),"multi-instance paths refused");
  Setup(OK);Check(efi_main((VOID *)1,&Table)==EFI_SUCCESS&&Captures==1&&Loads==1&&Starts==1,"incomplete capture still hands off to untouched Windows");
  Check(Stalls==2&&strstr(Screen,"DIAGNOSTIC v4")&&strstr(Screen,"Final capture status: 0x8000000000000006"),"version and actual EFI failure visible before Windows");
  Setup(NO_SPACE);Check(efi_main((VOID *)1,&Table)==EFI_SUCCESS&&Captures==0&&Loads==1&&Starts==1&&Creates==0,"low free space skips capture and starts Windows");
  for(int scenario=INFO_FAIL;scenario<=LOG_FAIL;scenario++) {
    Setup(scenario);Check(efi_main((VOID *)1,&Table)==EFI_SUCCESS&&Captures==0&&Reads==0&&Loads==1&&Starts==1,"bootstrap failure avoids graph/GPU reads and returns to Windows");
    Check(Stalls==2&&strstr(Screen,"Final capture status: 0x80000000000000"),"bootstrap failure status stays on screen");
    Check(!Pci.Pci.Write&&!Pci.Mem.Write&&!Bs.DisconnectController&&!Rt.SetVariable,"early diagnostics add no GPU or firmware writes");
  }
  Setup(MUTATE_IMAGE);memset(ImageBytes,0x5a,sizeof(ImageBytes));CAPTURE stable={0};stable.Table=&Table;stable.Complete=TRUE;stable.Io=EFI_SUCCESS;
  Check(GuProbeGpu(&Table,&stable)==EFI_SUCCESS&&stable.Complete&&Saves==2,"mutable firmware image saved from one consistent snapshot");
  Setup(OK);UINT32 cfg[64];PciRead(&Pci,EfiPciIoWidthUint32,0,64,cfg);EFI_ACPI_ADDRESS_SPACE_DESCRIPTOR *bar=NULL;UINT64 supports;
  Bar(&Pci,0,&supports,(VOID **)&bar);Check(PortBarValid(cfg,bar),"valid 32-bit BAR");
  cfg[4]|=4;cfg[5]=1;bar->AddrRangeMin+=0x100000000ULL;bar->AddrRangeMax+=0x100000000ULL;Check(PortBarValid(cfg,bar),"valid 64-bit BAR");
  bar->AddrTranslationOffset=1;Check(!PortBarValid(cfg,bar),"translated BAR rejected");bar->AddrTranslationOffset=0;
  bar->AddrLen=~(UINT64)0;Check(!PortBarValid(cfg,bar),"overflow/oversize rejected");free(bar);
  printf("PASS %d observer assertions; 13 probe + 6 bootstrap failure scenarios; persisted entry proof, visible status, Windows fallback; hardware access unavailable\n",Checks);
  return 0;
}
