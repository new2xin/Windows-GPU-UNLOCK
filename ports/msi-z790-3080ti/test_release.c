/* SPDX-License-Identifier: GPL-2.0-only. Real capture replay, host RAM only.
 * No live UEFI table, PCI device, physical address or driver is accessed. */
#include "../../internal-boot/boot/full-release.c"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "replay-fixture.h"
static GRAPH Live;
static EFI_BOOT_SERVICES Bs;
static EFI_SYSTEM_TABLE Table;
static EFI_PCI_IO_PROTOCOL Pci;
static int EnumerationFailures,EnumerationCalls,OpenFailures;
static EFI_STATUS EnumerationStatus;
static EFI_GRAPHICS_OUTPUT_PROTOCOL Gop;
static EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE Mode;
static EFI_GRAPHICS_OUTPUT_MODE_INFORMATION Info;
static EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL TextOut;
static EFI_SIMPLE_TEXT_INPUT_PROTOCOL Keys;
static EFI_DRIVER_BINDING_PROTOCOL Bindings[3];
static EFI_LOADED_IMAGE_PROTOCOL Images[3];
static UINT8 OutCode[GU_CONSOLE_TEXT_SIZE],GfxCode[GU_CONSOLE_GFX_SIZE],NvCode[GU_NVGOP_IMAGE_SIZE],Rom[1048576];
static UINT8 RtxPathCopy[36],OtherPathCopy[36];
static UINT32 Config[64];
static UINT64 Attr;
static int Scenario,Phase,Detaches,Enables,Claims,Closes,Checks;
static EFI_GUID Dummy={0x12345678,0,0,{0}};
static void Check(int ok,const char *message) {
  Checks++;if(!ok){fprintf(stderr,"FAIL scenario=%d phase=%d: %s\n",Scenario,Phase,message);exit(1);}
}
static EFI_STATUS EFIAPI Alloc(EFI_MEMORY_TYPE type,UINTN n,VOID **out){(void)type;*out=malloc(n);return *out?EFI_SUCCESS:EFI_OUT_OF_RESOURCES;}
static EFI_STATUS EFIAPI FreeMem(VOID *p){free(p);return EFI_SUCCESS;}
static EFI_TPL EFIAPI Raise(EFI_TPL value){(void)value;return TPL_APPLICATION;}
static VOID EFIAPI Restore(EFI_TPL value){(void)value;}
static EFI_STATUS EFIAPI Locate(EFI_LOCATE_SEARCH_TYPE search,EFI_GUID *guid,VOID *key,UINTN *n,EFI_HANDLE **out) {
  (void)key;*out=NULL;*n=0;
  if(search==AllHandles){*n=FX_HANDLES+64;*out=malloc(*n*sizeof(**out));for(UINTN i=0;i<*n;i++)(*out)[i]=(EFI_HANDLE)(i+1);return EFI_SUCCESS;}
  if(GuGuidEqual(guid,&GopGuid)) {
    if(Phase>=3 && Scenario!=17)return EFI_NOT_FOUND;
    *n=Scenario==2?2:1;*out=malloc(*n*sizeof(**out));for(UINTN i=0;i<*n;i++)(*out)[i]=FX_Rtx;return EFI_SUCCESS;
  }
  if(GuGuidEqual(guid,&PciGuid)){*n=1;*out=malloc(sizeof(**out));(*out)[0]=FX_RtxPci;return EFI_SUCCESS;}
  return EFI_UNSUPPORTED;
}
static EFI_STATUS EFIAPI Protocols(EFI_HANDLE h,EFI_GUID ***out,UINTN *n) {
  if(h==(EFI_HANDLE)1){EnumerationCalls++;if(EnumerationFailures){EnumerationFailures--;*out=NULL;*n=0;return EnumerationStatus;}}
  *out=malloc(256*sizeof(**out));*n=1;(*out)[0]=&Dummy;
  for(UINTN i=0;i<Live.Count;i++)if(Live.Edges[i].Handle==h) {
    BOOLEAN seen=FALSE;for(UINTN j=0;j<*n;j++)if(GuGuidEqual((*out)[j],&Live.Edges[i].Protocol))seen=TRUE;
    if(!seen)(*out)[(*n)++]=&Live.Edges[i].Protocol;
  }
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Opens(EFI_HANDLE h,EFI_GUID *guid,EFI_OPEN_PROTOCOL_INFORMATION_ENTRY **out,UINTN *n) {
  if(h==FX_UsbHost && OpenFailures){OpenFailures--;*out=NULL;*n=0;return EFI_NOT_FOUND;}
  *out=malloc(512*sizeof(**out));*n=0;
  for(UINTN i=0;i<Live.Count;i++){EDGE *e=&Live.Edges[i];if(e->Handle==h && GuGuidEqual(guid,&e->Protocol))(*out)[(*n)++]=(EFI_OPEN_PROTOCOL_INFORMATION_ENTRY){e->Agent,e->Controller,e->Attributes,e->Count};}
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Handle(EFI_HANDLE h,EFI_GUID *guid,VOID **out) {
  *out=NULL;
  const EFI_HANDLE agents[]={FX_OutAgent,FX_GfxAgent,FX_NvAgent},images[]={FX_OutImage,FX_GfxImage,FX_NvImage};
  for(UINTN i=0;i<3;i++) {
    if(h==agents[i] && GuGuidEqual(guid,&BindingGuid))*out=&Bindings[i];
    if(h==images[i] && GuGuidEqual(guid,&ImageGuid))*out=&Images[i];
  }
  if(h==FX_RtxPci && GuGuidEqual(guid,&PciGuid))*out=&Pci;
  if(h==FX_Rtx && Phase<3 && GuGuidEqual(guid,&GopGuid))*out=&Gop;
  if(h==FX_Rtx && Phase<3 && GuGuidEqual(guid,&PathGuid))*out=RtxPathCopy;
  if(h==FX_OtherChild && Phase<3 && GuGuidEqual(guid,&PathGuid))*out=OtherPathCopy;
  if((h==FX_Rtx || h==FX_OtherChild) && Phase<3 && GuGuidEqual(guid,&EdidGuid))*out=&Dummy;
  if(GuGuidEqual(guid,&TextGuid) && (h==FX_Console || (h==FX_Rtx && Phase<2)))*out=&TextOut;
  if(h==FX_Input && GuGuidEqual(guid,&InputGuid))*out=&Keys;
  return *out?EFI_SUCCESS:EFI_UNSUPPORTED;
}
static EFI_STATUS EFIAPI FindPci(EFI_GUID *guid,EFI_DEVICE_PATH_PROTOCOL **rest,EFI_HANDLE *h) {
  Check(GuGuidEqual(guid,&PciGuid),"locate only PCI parent");*h=FX_RtxPci;*rest=(VOID *)((UINT8 *)*rest+24);return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Location(EFI_PCI_IO_PROTOCOL *io,UINTN *s,UINTN *b,UINTN *d,UINTN *f){Check(io==&Pci,"exact PciIo");*s=*d=*f=0;*b=Scenario==1?2:1;return EFI_SUCCESS;}
static EFI_STATUS EFIAPI ReadConfig(EFI_PCI_IO_PROTOCOL *io,EFI_PCI_IO_PROTOCOL_WIDTH width,UINT32 at,UINTN n,VOID *out) {
  Check(io==&Pci && width==EfiPciIoWidthUint32 && !at && (n==16 || n==64),"bounded PCI read");memcpy(out,Config,n*4);return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI ReadMem(EFI_PCI_IO_PROTOCOL *io,EFI_PCI_IO_PROTOCOL_WIDTH width,UINT8 bar,UINT64 off,UINTN n,VOID *out) {
  Check(io==&Pci && !bar,"exact BAR0 only");
  if(width==EfiPciIoWidthUint8 && off>=0x300000 && off+n<=0x400000){memcpy(out,Rom+off-0x300000,n);if(Scenario==8)((UINT8 *)out)[0]^=1;return EFI_SUCCESS;}
  Check(width==EfiPciIoWidthUint32 && n==1,"single cold register");
  for(UINTN i=0;i<9;i++)if(off==ColdOffsets[i]){*(UINT32 *)out=FixtureCold[i];if(Scenario==9 && i==4)*(UINT32 *)out=1;return EFI_SUCCESS;}
  return EFI_UNSUPPORTED;
}
static EFI_STATUS EFIAPI Bar(EFI_PCI_IO_PROTOCOL *io,UINT8 index,UINT64 *supports,VOID **out) {
  Check(io==&Pci && !index,"exact BAR description");if(supports)*supports=0;
  UINT8 *raw=calloc(1,sizeof(EFI_ACPI_ADDRESS_SPACE_DESCRIPTOR)+2);*out=raw;EFI_ACPI_ADDRESS_SPACE_DESCRIPTOR *d=(VOID *)raw;
  d->Desc=ACPI_ADDRESS_SPACE_DESCRIPTOR;d->Len=sizeof(*d)-3;d->AddrSpaceGranularity=32;
  d->AddrRangeMin=GU_BAR0_BASE+(Scenario==10?0x1000000:0);d->AddrRangeMax=GU_BAR0_END;d->AddrLen=0x1000000;raw[sizeof(*d)]=ACPI_END_TAG_DESCRIPTOR;return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Attributes(EFI_PCI_IO_PROTOCOL *io,EFI_PCI_IO_PROTOCOL_ATTRIBUTE_OPERATION operation,UINT64 bits,UINT64 *out) {
  Check(io==&Pci,"attributes target");
  if(operation==EfiPciIoAttributeOperationGet){*out=Attr;return EFI_SUCCESS;}
  Check(Phase==3,"attribute writes forbidden before release");
  if(operation==EfiPciIoAttributeOperationSupported){*out=0x700;return EFI_SUCCESS;}
  Check(operation==EfiPciIoAttributeOperationEnable && bits && !(bits&~0x300ULL) && Full.Owned,"only bounded IO/MEM restore");Enables++;Attr|=bits;Config[1]|=3;
  return Scenario==18?EFI_DEVICE_ERROR:EFI_SUCCESS;
}
static void Drop(EFI_HANDLE handle,EFI_HANDLE agent) {
  UINTN n=0;for(UINTN i=0;i<Live.Count;i++)if(!(Live.Edges[i].Handle==handle && Live.Edges[i].Agent==agent))Live.Edges[n++]=Live.Edges[i];Live.Count=n;
}
static void DropUsbChildren(void) {
  const EFI_GUID usb={0x3e745226,0x9818,0x45b6,{0xa2,0xac,0xd7,0xcd,0x0e,0x8b,0xa2,0xbc}};
  UINTN n=0,removed=0;
  for(UINTN i=0;i<Live.Count;i++) {
    EDGE *e=&Live.Edges[i];
    if(removed<3 && e->Attributes==EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER && GuGuidEqual(&e->Protocol,&usb)){removed++;continue;}
    Live.Edges[n++]=*e;
  }
  Check(removed==3,"real fixture contains three USB host child opens");Live.Count=n;
}
static void RemapUsbLeaves(void) {
  for(UINTN i=0;i<Live.Count;i++)for(UINTN j=0;j<3;j++)if(Live.Edges[i].Handle==FX_UsbHost && Live.Edges[i].Controller==FixtureUsbLeaves[j])Live.Edges[i].Controller=(EFI_HANDLE)(FX_HANDLES+1+j);
}
static void ChangeGpuCount(void) {
  for(UINTN i=0;i<Live.Count;i++)if(Live.Edges[i].Handle==FX_RtxPci){Live.Edges[i].Count++;return;}
  Check(0,"GPU dependency exists for mutation");
}
static void AmbientChange(int mode) {
  if(mode==29)Live.Edges[Live.Count++]=(EDGE){FX_UsbHost,FX_UsbAgent,(EFI_HANDLE)(FX_HANDLES+10),Dummy,8,1};
  if(mode==30)Drop(FX_UsbHost,FX_UsbAgent);
  if(mode==31)Live.Edges[0].Count++;
  if(mode==32)Live.Edges[Live.Count++]=(EDGE){FX_UsbLeaf0,(EFI_HANDLE)(FX_HANDLES+11),FX_UsbLeaf0,Dummy,16,1};
  if(mode==33)for(UINTN i=0;i<Live.Count/2;i++){EDGE t=Live.Edges[i];Live.Edges[i]=Live.Edges[Live.Count-1-i];Live.Edges[Live.Count-1-i]=t;}
  if(mode==34)for(UINTN i=0;i<Live.Count;i++)if(Live.Edges[i].Handle==FX_UsbHost)Live.Edges[i].Agent=(EFI_HANDLE)(FX_HANDLES+12);
  if(mode==35){DropUsbChildren();Live.Edges[Live.Count++]=(EDGE){(EFI_HANDLE)(FX_HANDLES+20),(EFI_HANDLE)(FX_HANDLES+21),(EFI_HANDLE)(FX_HANDLES+22),Dummy,16,1};}
  if(mode==36)Live.Edges[Live.Count++]=(EDGE){FX_UsbHost,FX_Input,FX_UsbHost,Dummy,16,1};
  if(mode==37)Live.Edges[Live.Count++]=(EDGE){FX_UsbHost,FX_UsbAgent,FX_OtherChild,Dummy,8,1};
  if(mode==38)Live.Edges[Live.Count++]=(EDGE){FX_RtxPci,FX_UsbAgent,FX_RtxPci,PciGuid,16,1};
  if(mode==39)Live.Edges[Live.Count++]=(EDGE){FX_OtherChild,FX_UsbAgent,FX_OtherChild,Dummy,16,1};
  if(mode==40)for(UINTN i=0;i<Live.Count;i++)if(Live.Edges[i].Handle==FX_RtxPci){Live.Edges[Live.Count++]=Live.Edges[i];break;}
  if(mode==41)Keys.WaitForKey=NULL;
  if(mode==42){Live.Edges[Live.Count++]=(EDGE){FX_RtxPci,FX_NvAgent,(EFI_HANDLE)(FX_HANDLES+23),PathGuid,8,1};Live.Edges[Live.Count++]=(EDGE){FX_UsbHost,FX_UsbAgent,(EFI_HANDLE)(FX_HANDLES+23),Dummy,8,1};}
  if(mode==44)Info.HorizontalResolution++;
}
static EFI_STATUS EFIAPI Disconnect(EFI_HANDLE h,EFI_HANDLE agent,EFI_HANDLE child) {
  Check(!child,"no arbitrary child selection");Detaches++;
  if(Phase<2) {
    Check(h==FX_Rtx && agent==(Phase?FX_GfxAgent:FX_OutAgent),"ordered console detach only");
    if(Scenario==13)return EFI_DEVICE_ERROR;
    Drop(h,agent);Phase++;return EFI_SUCCESS;
  }
  Check(Phase==2 && h==FX_RtxPci && agent==FX_NvAgent,"NVIDIA stop directly after console; no EDID stop");
  if(Scenario==14)return EFI_DEVICE_ERROR;
  if(Scenario==15){Drop(h,agent);return EFI_DEVICE_ERROR;}
  Drop(h,agent);Phase=3;Config[1]&=~3U;Attr=0;
  if(Scenario==16)Config[4]^=0x1000000;
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Open(EFI_HANDLE h,EFI_GUID *guid,VOID **out,EFI_HANDLE agent,EFI_HANDLE controller,UINT32 flags) {
  Check(Phase==3 && h==FX_RtxPci && GuGuidEqual(guid,&PciGuid) && agent==(VOID *)9999 && !controller && flags==EFI_OPEN_PROTOCOL_EXCLUSIVE,"bounded exclusive after NV detach");
  Claims++;*out=&Pci;Live.Edges[Live.Count++]=(EDGE){h,agent,NULL,*guid,flags,1};return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Close(EFI_HANDLE h,EFI_GUID *guid,EFI_HANDLE agent,EFI_HANDLE controller){(void)guid;(void)controller;Closes++;Drop(h,agent);return EFI_SUCCESS;}
static EFI_STATUS EFIAPI Reset(EFI_SIMPLE_TEXT_INPUT_PROTOCOL *k,BOOLEAN extended){(void)k;(void)extended;return EFI_SUCCESS;}
static EFI_STATUS EFIAPI ReadKey(EFI_SIMPLE_TEXT_INPUT_PROTOCOL *k,EFI_INPUT_KEY *key){(void)k;(void)key;return EFI_NOT_READY;}
static EFI_STATUS Log(VOID *context,const char *event,EFI_STATUS status) {
  (void)context;
  if(status!=EFI_SUCCESS && !Scenario)fprintf(stderr,"event=%s status=%llx\n",event,(unsigned long long)status);
  if((Scenario==11 || Scenario==46) && !strcmp(event,"capture-cold"))ChangeGpuCount();
  if(Scenario==12 && !strcmp(event,"review-original-graph"))return EFI_DEVICE_ERROR;
  if(Scenario==19 && !strcmp(event,"detach-nvidia-gop-intent"))NvCode[GU_NVGOP_STOP]^=1;
  if(Scenario==20 && !strcmp(event,"detach-nvidia-gop-intent"))OtherPathCopy[28]^=1;
  if(Scenario==21 && !strcmp(event,"nvidia-detach-verified"))Live.Edges[Live.Count++]=(EDGE){FX_RtxPci,FX_Input,FX_RtxPci,PciGuid,16,1};
  if(Scenario==22 && !strcmp(event,"pci-decode-restore-intent-io-mem-only"))Keys.WaitForKey=NULL;
  if(Scenario==23 && !strcmp(event,"capture-cold"))DropUsbChildren();
  if(Scenario==24 && !strcmp(event,"detach-nvidia-gop-intent"))DropUsbChildren();
  if(Scenario==25 && !strcmp(event,"detach-nvidia-gop-intent"))Config[50]^=1;
  if(Scenario==26 && !strcmp(event,"capture-cold"))RemapUsbLeaves();
  if(Scenario==27 && !strcmp(event,"detach-nvidia-gop-intent"))RemapUsbLeaves();
  if(Scenario==28 && !strcmp(event,"detach-nvidia-return"))RemapUsbLeaves();
  if(Scenario>=29 && Scenario<=44 && !strcmp(event,"detach-nvidia-gop-intent"))AmbientChange(Scenario);
  if(Scenario==43 && !strcmp(event,"detach-rtx-textout-intent"))RemapUsbLeaves();
  return EFI_SUCCESS;
}
static void Setup(int scenario) {
  Scenario=scenario;Phase=Detaches=Enables=Claims=Closes=EnumerationFailures=EnumerationCalls=OpenFailures=0;EnumerationStatus=EFI_NOT_FOUND;
  Live.Count=sizeof(FixtureEdges)/sizeof(FixtureEdges[0]);memcpy(Live.Edges,FixtureEdges,sizeof(FixtureEdges));
  memcpy(Config,FixtureConfig,sizeof(Config));Attr=0x300;
  memcpy(RtxPathCopy,FixtureRtxPath,36);memcpy(OtherPathCopy,FixtureOtherChildPath,36);
  Bs=(EFI_BOOT_SERVICES){0};Table=(EFI_SYSTEM_TABLE){0};Pci=(EFI_PCI_IO_PROTOCOL){0};
  Bs.AllocatePool=Alloc;Bs.FreePool=FreeMem;Bs.LocateHandleBuffer=Locate;Bs.ProtocolsPerHandle=Protocols;Bs.OpenProtocolInformation=Opens;
  Bs.HandleProtocol=Handle;Bs.LocateDevicePath=FindPci;Bs.RaiseTPL=Raise;Bs.RestoreTPL=Restore;
  Pci.Pci.Read=ReadConfig;Pci.GetLocation=Location;Pci.Mem.Read=ReadMem;Pci.GetBarAttributes=Bar;Pci.Attributes=Attributes;
  Info=(EFI_GRAPHICS_OUTPUT_MODE_INFORMATION){0};Info.HorizontalResolution=2560;Info.VerticalResolution=1440;Info.PixelsPerScanLine=4096;
  Mode=(EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE){0};Mode.Info=&Info;Mode.SizeOfInfo=sizeof(Info);Mode.MaxMode=13;Mode.FrameBufferBase=0x6000000000;Mode.FrameBufferSize=0x1680000;Gop.Mode=&Mode;
  Keys=(EFI_SIMPLE_TEXT_INPUT_PROTOCOL){0};Keys.Reset=Reset;Keys.ReadKeyStroke=ReadKey;Keys.WaitForKey=(VOID *)1;
  Table.BootServices=&Bs;Table.ConsoleOutHandle=FX_Console;Table.StandardErrorHandle=FX_Error;Table.ConsoleInHandle=FX_Input;
  Table.ConOut=Table.StdErr=&TextOut;Table.ConIn=&Keys;
  EFI_HANDLE agents[]={FX_OutAgent,FX_GfxAgent,FX_NvAgent},images[]={FX_OutImage,FX_GfxImage,FX_NvImage};
  UINT8 *memory[]={OutCode,GfxCode,NvCode};UINTN sizes[]={sizeof(OutCode),sizeof(GfxCode),sizeof(NvCode)},stops[]={GU_CONSOLE_TEXT_STOP,GU_CONSOLE_GFX_STOP,GU_NVGOP_STOP};
  const UINT8 *paths[]={FixtureOutFilePath,FixtureGfxFilePath,FixtureNvFilePath};
  for(UINTN i=0;i<3;i++) {
    Bindings[i]=(EFI_DRIVER_BINDING_PROTOCOL){0};Bindings[i].DriverBindingHandle=agents[i];Bindings[i].ImageHandle=images[i];Bindings[i].Version=i==2?GU_NVGOP_VERSION:0x10;
    Bindings[i].Stop=(EFI_DRIVER_BINDING_STOP)(memory[i]+stops[i]);Images[i]=(EFI_LOADED_IMAGE_PROTOCOL){0};
    Images[i].ImageBase=memory[i];Images[i].ImageSize=sizes[i];Images[i].ImageCodeType=EfiBootServicesCode;Images[i].FilePath=(VOID *)paths[i];
  }Images[2].DeviceHandle=FX_RtxPci;
  if(scenario==3)Bindings[0].Version++;
  if(scenario==4)Live.Edges[Live.Count++]=(EDGE){FX_OtherChild,FX_Input,FX_OtherChild,EdidGuid,16,1};
  if(scenario==5)Live.Edges[Live.Count++]=(EDGE){FX_RtxPci,FX_Input,FX_RtxPci,PciGuid,16,1};
  if(scenario==6)Config[11]^=1;
  if(scenario==7)Config[1]|=4;
}
static void Load(const char *root,const char *name,VOID *data,UINTN size) {
  char path[2048];snprintf(path,sizeof(path),"%s/%s",root,name);FILE *file=fopen(path,"rb");
  Check(file!=NULL,"fixture exists");Check(fread(data,1,size,file)==size && fgetc(file)==EOF,"exact immutable fixture bytes");fclose(file);
}
static void ScopeProperties(void) {
  Setup(0);PLAN p={0};
  p.Console=FX_Console;p.Error=FX_Error;p.Input=FX_Input;p.Rtx=FX_Rtx;p.RtxPci=FX_RtxPci;p.OutAgent=FX_OutAgent;p.GfxAgent=FX_GfxAgent;
  static GRAPH original,current;original=Live;MSI_GRAPH_SCOPE scope;
  Check(MsiBuildScope(&p,&original,&original,&scope),"bounded dependency scope built from real topology");
  Check(ExpectedGraph(&p,&original,&original,0),"identity projection accepts real topology");
  for(UINTN i=0;i<original.Count;i++) {
    BOOLEAN sensitive=MsiScopedEdge(&scope,&original.Edges[i]);
    current=original;current.Edges[i].Count++;
    Check(ExpectedGraph(&p,&original,&current,0)==!sensitive,"every real edge count: protected changes rejected, unrelated changes accepted");
    current=original;current.Edges[i]=current.Edges[--current.Count];
    Check(ExpectedGraph(&p,&original,&current,0)==!sensitive,"every real edge removal is classified by dependency");
    current=original;current.Edges[current.Count++]=original.Edges[i];
    Check(ExpectedGraph(&p,&original,&current,0)==!sensitive,"duplicate protected edges cannot be hidden by counts");
  }
  for(UINTN i=0;i<scope.Count;i++)for(UINTN position=0;position<3;position++) {
    current=original;EDGE e={(EFI_HANDLE)9000,(EFI_HANDLE)9001,(EFI_HANDLE)9002,Dummy,16,1};
    if(!position)e.Handle=scope.Handles[i];else if(position==1)e.Agent=scope.Handles[i];else e.Controller=scope.Handles[i];
    current.Edges[current.Count++]=e;
    Check(!ExpectedGraph(&p,&original,&current,0),"new incoming or outgoing edge touching ANY protected role rejected");
  }
  current.Count=0;
  for(UINTN i=0;i<original.Count;i++)if(MsiScopedEdge(&scope,&original.Edges[i]))current.Edges[current.Count++]=original.Edges[i];
  Check(ExpectedGraph(&p,&original,&current,0),"all unrelated dependencies may disappear");
  for(UINTN i=0;i<256;i++)current.Edges[current.Count++]=(EDGE){(EFI_HANDLE)(9000+i),(EFI_HANDLE)9998,(EFI_HANDLE)(9000+i),Dummy,16,1};
  Check(ExpectedGraph(&p,&original,&current,0),"256 new unrelated devices do not block GPU release");
  current=original;
  for(UINTN i=0;i<40;i++)current.Edges[current.Count++]=(EDGE){FX_RtxPci,FX_NvAgent,(EFI_HANDLE)(9000+i),PathGuid,8,1};
  Check(!ExpectedGraph(&p,&original,&current,0),"dependency scope overflow rejects instead of dropping protected handles");
  PLAN shifted=p;EFI_HANDLE *roots[]={&shifted.Console,&shifted.Error,&shifted.Input,&shifted.Rtx,&shifted.RtxPci,&shifted.OutAgent,&shifted.GfxAgent};
  for(UINTN i=0;i<sizeof(roots)/sizeof(roots[0]);i++)*roots[i]=(EFI_HANDLE)((UINTN)*roots[i]+5000);
  for(UINTN i=0;i<original.Count;i++){original.Edges[i].Handle=(EFI_HANDLE)((UINTN)original.Edges[i].Handle+5000);original.Edges[i].Agent=(EFI_HANDLE)((UINTN)original.Edges[i].Agent+5000);original.Edges[i].Controller=(EFI_HANDLE)((UINTN)original.Edges[i].Controller+5000);}
  Check(ExpectedGraph(&shifted,&original,&original,0),"different boot handle addresses do not use a captured handle baseline");
}
int main(int argc,char **argv) {
  Check(argc==2,"capture directory required");Load(argv[1],FX_OutFile,OutCode,sizeof(OutCode));Load(argv[1],FX_GfxFile,GfxCode,sizeof(GfxCode));Load(argv[1],FX_NvFile,NvCode,sizeof(NvCode));Load(argv[1],"PROM.BIN",Rom,sizeof(Rom));
  Setup(0);Check(GuReviewReleasePlan(&Table,Log,NULL)==EFI_SUCCESS,"actual graph/code/PROM/cold reviewed without detach APIs");
  Check(!Detaches && !Enables && !Claims && !PreparedReady && !Full.Captured && !Full.Table && !PreparedGraph,"read-only review leaves no mutation or authority");
  for(int s=1;s<=12;s++){Setup(s);Check(GuReviewReleasePlan(&Table,Log,NULL)!=EFI_SUCCESS,"bad BDF/GOP/code/owner/identity/BME/PROM/cold/BAR/late graph/log rejected");Check(!Detaches && !Enables && !Claims && !PreparedReady && !Full.Captured,"failed review never grants authority");}
  UINT8 *code[]={OutCode,GfxCode,NvCode};UINTN rva[]={GU_CONSOLE_TEXT_RVA,GU_CONSOLE_GFX_RVA,0x280};
  for(UINTN i=0;i<3;i++){Setup(0);code[i][rva[i]]^=1;Check(GuReviewReleasePlan(&Table,Log,NULL)!=EFI_SUCCESS,"each modified code image rejected");code[i][rva[i]]^=1;}
  Setup(0);Bs.DisconnectController=Disconnect;Bs.OpenProtocol=Open;Bs.CloseProtocol=Close;
  Check(GuPrepareConsole(&Table,Log,NULL)==EFI_SUCCESS,"exact console preparation replay");
  Check(GuReleaseGop(&Table,(VOID *)9999,Log,NULL)==EFI_SUCCESS,"zero EDID consumer release and decode restore replay");
  Check(Detaches==3 && Enables==1 && Claims==1 && Closes==1 && Full.EdidDetached==0 && Full.ChildCount==2,"only two console + one NV stops; no runtime EDID stop");
  Check(GuVerifyReleased(&Table,TRUE)==EFI_SUCCESS,"final topology/config/cold verification");
  for(int s=13;s<=22;s++) {
    Setup(s);Bs.DisconnectController=Disconnect;Bs.OpenProtocol=Open;Bs.CloseProtocol=Close;
    EFI_STATUS result=GuPrepareConsole(&Table,Log,NULL);
    if(result==EFI_SUCCESS)result=GuReleaseGop(&Table,(VOID *)9999,Log,NULL);
    Check(result!=EFI_SUCCESS,"detach/partial effect/late code/path/new owner/input failures stop");
    if(s!=18)Check(!Enables,"failure cannot enable PCI decode");
    if(s==19)NvCode[GU_NVGOP_STOP]^=1;
  }
  Setup(23);
  Check(GuReviewReleasePlan(&Table,Log,NULL)==EFI_SUCCESS,"observed unrelated USB disappearance no longer prevents read-only review");
  Check(!Detaches && !Enables && !Claims && !PreparedReady && !Full.Table,"successful review leaves no mutation authority");
  for(int s=24;s<=25;s++){
    Setup(s);Bs.DisconnectController=Disconnect;Bs.OpenProtocol=Open;Bs.CloseProtocol=Close;
    Check(GuPrepareConsole(&Table,Log,NULL)==EFI_SUCCESS,"console prepare for late recheck failure");
    EFI_STATUS result=GuReleaseGop(&Table,(VOID *)9999,Log,NULL);
    if(s==24)Check(result==EFI_SUCCESS && Detaches==3 && Enables==1,"late unrelated USB disappearance permits the real GPU release path");
    else {Check(result==EFI_NOT_READY && Detaches==2 && !Enables && !Claims,"GPU config drift stops before NVIDIA stop");Check(ReleaseRecheckStage && !strcmp(ReleaseRecheckStage,"release-recheck-pci-match"),"precise failed PCI predicate recorded");}
  }
  Setup(26);Check(GuReviewReleasePlan(&Table,Log,NULL)==EFI_SUCCESS,"three equivalent USB handles can change during read-only review");
  Check(!Detaches && !Enables && !Claims && !Full.Table && !PreparedReady,"equivalent review grants no authority");
  for(int s=27;s<=44;s++) {
    Setup(s);Bs.DisconnectController=Disconnect;Bs.OpenProtocol=Open;Bs.CloseProtocol=Close;
    Check(GuPrepareConsole(&Table,Log,NULL)==EFI_SUCCESS,"fresh console preparation with USB policy");
    EFI_STATUS result=GuReleaseGop(&Table,(VOID *)9999,Log,NULL);
    if(s<=35 || s==43){Check(result==EFI_SUCCESS,"unrelated USB replacement/removal/addition/driver/owner/order changes accepted");Check(Detaches==3 && Enables==1 && Claims==1 && Closes==1,"only original GPU mutations; USB never stopped");Check(GuVerifyReleased(&Table,TRUE)==EFI_SUCCESS,"scoped graph remains valid for original unlock handoff");}
    else {Check(result!=EFI_SUCCESS,"new protected incoming/outgoing dependency, duplicate, owner, input or display change rejected");Check(Detaches==2 && !Enables && !Claims,"protected fault cannot reach NVIDIA detach or decode");}
  }
  Setup(45);Drop(FX_UsbHost,FX_UsbAgent);
  Check(GuReviewReleasePlan(&Table,Log,NULL)==EFI_SUCCESS,"ASMedia leaves may all be absent at boot");
  Check(!Detaches && !Enables && !Claims && !Full.Table && !PreparedReady,"missing unrelated devices confer no authority");
  Setup(46);Check(GuReviewReleasePlan(&Table,Log,NULL)!=EFI_SUCCESS && !Detaches && !Claims && !Enables,"late protected drift still rejects read-only review");
  static GRAPH snapshot;
  Setup(47);EnumerationFailures=1;Check(Snapshot(&Bs,&snapshot)==EFI_SUCCESS && EnumerationCalls==2 && snapshot.Count==Live.Count,"vanishing handle enumeration retried once with a fresh complete result");
  Setup(48);EnumerationFailures=10;EnumerationStatus=EFI_NOT_READY;Check(Snapshot(&Bs,&snapshot)==EFI_NOT_READY && EnumerationCalls==3 && !snapshot.Count,"persistent enumeration failure bounded to three attempts; partial graph discarded");
  Setup(49);EnumerationFailures=10;EnumerationStatus=EFI_DEVICE_ERROR;Check(Snapshot(&Bs,&snapshot)==EFI_DEVICE_ERROR && EnumerationCalls==1 && !snapshot.Count,"non-transient service error is never retried or accepted");
  Setup(50);OpenFailures=1;Check(Snapshot(&Bs,&snapshot)==EFI_SUCCESS && EnumerationCalls==2 && snapshot.Count==Live.Count,"protocol disappearance after a partial walk does not duplicate earlier edges");
  ScopeProperties();
  printf("PASS %d host assertions: real release replay, ambient topology changes, protected boundary mutations, bounded transient enumeration and 3 code mutations; %u real handles / %zu active edges; no GPU access\n",Checks,FX_HANDLES,sizeof(FixtureEdges)/sizeof(FixtureEdges[0]));return 0;
}
